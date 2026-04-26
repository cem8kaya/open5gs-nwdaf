#include "nwdaf_collector.hpp"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <chrono>
#include <thread>
#include <cstdio>
#include <array>
#include <algorithm>
#include <unistd.h>
#include <climits>

#ifdef NWDAF_HAS_SQLITE
#include <sqlite3.h>
#endif

#ifdef NWDAF_USE_SD_JOURNAL
#include <systemd/sd-journal.h>
#endif

static std::string nowISO() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    gmtime_r(&t, &tm_buf);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return buf;
}

NwdafCollector::NwdafCollector(const NwdafConfig& config)
    : config_(config),
      dl_ewma_(config.ewma_alpha),
      ul_ewma_(config.ewma_alpha)
{
    initMongo();
#ifdef NWDAF_HAS_SQLITE
    initHistoryDb();  // PROD-01
#endif
}

NwdafCollector::~NwdafCollector() {
    stopBackgroundCollection();
#ifdef NWDAF_HAS_SQLITE
    if (history_db_) { sqlite3_close(history_db_); history_db_ = nullptr; }
#endif
}

void NwdafCollector::initMongo() {
#ifdef NWDAF_HAS_MONGODB
    try {
        // BUG-05: static instance created exactly once per process lifetime;
        // mongocxx::instance::current() would throw if called before construction.
        static mongocxx::instance instance{};
        mongo_client_ = std::make_unique<mongocxx::client>(
            mongocxx::uri{config_.mongodb_uri});
        spdlog::info("MongoDB connected: {}", config_.mongodb_uri);
    } catch (const std::exception& e) {
        spdlog::warn("MongoDB init failed: {} — subscriber count will return 0", e.what());
    }
#endif
}

// ── journald reading ─────────────────────────────────────────────────────────

std::vector<std::string> NwdafCollector::readJournalLines(const std::string& unit, int n) {
    std::vector<std::string> lines;

#ifdef NWDAF_USE_SD_JOURNAL
    sd_journal* j = nullptr;
    if (sd_journal_open(&j, SD_JOURNAL_LOCAL_ONLY) < 0) {
        spdlog::warn("sd_journal_open failed for unit {}", unit);
        return lines;
    }
    std::string match = "_SYSTEMD_UNIT=open5gs-" + unit + ".service";
    sd_journal_add_match(j, match.c_str(), match.size());
    sd_journal_seek_tail(j);
    int count = 0;
    while (count < n && sd_journal_previous(j) > 0) {
        const void* data = nullptr;
        size_t len = 0;
        if (sd_journal_get_data(j, "MESSAGE", &data, &len) == 0) {
            // data is "MESSAGE=<text>"
            const char* msg = static_cast<const char*>(data) + 8; // skip "MESSAGE="
            lines.emplace_back(msg, len - 8);
            ++count;
        }
    }
    sd_journal_close(j);
    std::reverse(lines.begin(), lines.end());
#else
    // --since '1 hour ago' caps the window so event counters (e.g. registrationCount)
    // reflect recent activity, not lifetime accumulation since boot.
    std::string cmd = "journalctl -u open5gs-" + unit + " -n " +
                      std::to_string(n) + " --since '1 hour ago'"
                      " --no-pager --output=short-iso 2>/dev/null";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) {
        spdlog::warn("popen failed for journalctl unit {}", unit);
        return lines;
    }
    char buf[2048];
    while (fgets(buf, sizeof(buf), fp)) {
        std::string line(buf);
        if (!line.empty() && line.back() == '\n') line.pop_back();
        lines.push_back(std::move(line));
    }
    pclose(fp);
#endif
    return lines;
}

// ── /proc helpers ─────────────────────────────────────────────────────────────

std::pair<long,long> NwdafCollector::readProcStat(int pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    if (!f) return {0, 0};
    std::string line;
    std::getline(f, line);
    // fields are space-separated; utime=14th, stime=15th (0-indexed: 13,14)
    std::istringstream ss(line);
    std::string tok;
    long utime = 0, stime = 0;
    for (int i = 0; ss >> tok; ++i) {
        if (i == 13) utime = std::stol(tok);
        if (i == 14) { stime = std::stol(tok); break; }
    }
    return {utime, stime};
}

long NwdafCollector::readProcMemKb(int pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/status");
    if (!f) return 0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            long kb = 0;
            sscanf(line.c_str(), "VmRSS: %ld kB", &kb);
            return kb;
        }
    }
    return 0;
}

// ── BUG-01: rate-based CPU helpers ───────────────────────────────────────────

std::chrono::steady_clock::time_point NwdafCollector::getCpuNow() const {
    return std::chrono::steady_clock::now();
}

double NwdafCollector::computeCpuPct(int pid) {
    auto [utime, stime] = readProcStat(pid);
    long total_ticks = utime + stime;
    auto now = getCpuNow();

    auto it = cpu_snapshots_.find(pid);
    if (it == cpu_snapshots_.end()) {
        cpu_snapshots_[pid] = {total_ticks, now};
        return 0.0;  // no baseline yet — first observation
    }

    auto elapsed_s = std::chrono::duration<double>(now - it->second.ts).count();
    if (elapsed_s < 0.01) {
        return 0.0;  // guard against near-zero elapsed (clock resolution)
    }

    long sc = sysconf(_SC_CLK_TCK);
    if (sc <= 0) sc = 100;

    double pct = 100.0 * (double)(total_ticks - it->second.ticks) / (elapsed_s * (double)sc);
    cpu_snapshots_[pid] = {total_ticks, now};
    return std::min(100.0, std::max(0.0, pct));
}

// ── /sys/class/net helpers ────────────────────────────────────────────────────

std::pair<uint64_t,uint64_t> NwdafCollector::readNetStats(const std::string& iface) {
    auto readFile = [](const std::string& path) -> uint64_t {
        std::ifstream f(path);
        if (!f) return 0;
        uint64_t v = 0;
        f >> v;
        return v;
    };
    std::string base = "/sys/class/net/" + iface + "/statistics/";
    return {readFile(base + "rx_bytes"), readFile(base + "tx_bytes")};
}

// ── Blocking collectors ───────────────────────────────────────────────────────

std::vector<AmfEvent> NwdafCollector::collectAmfEvents() {
    std::vector<AmfEvent> events;
    auto service_it = config_.nf_service_names.find("AMF");
    if (service_it == config_.nf_service_names.end()) return events;

    auto lines = readJournalLines(service_it->second, config_.amf_journal_lines);
    std::regex supi_re(config_.supi_regex);

    for (const auto& line : lines) {
        AmfEvent ev;
        ev.raw_line = line;
        ev.timestamp_iso = nowISO();

        if (line.find("Registration") != std::string::npos ||
            line.find("registration") != std::string::npos)
            ev.event_type = "REGISTRATION";
        else if (line.find("Deregistration") != std::string::npos ||
                 line.find("deregistration") != std::string::npos)
            ev.event_type = "DEREGISTRATION";
        else if (line.find("Authentication") != std::string::npos &&
                 line.find("success") != std::string::npos)
            ev.event_type = "AUTH_SUCCESS";
        else if (line.find("Authentication") != std::string::npos &&
                 line.find("fail") != std::string::npos)
            ev.event_type = "AUTH_FAILURE";
        else if (line.find("Handover") != std::string::npos ||
                 line.find("handover") != std::string::npos)
            ev.event_type = "HANDOVER";
        else
            continue;

        std::smatch m;
        if (std::regex_search(line, m, supi_re))
            ev.supi = "imsi-" + m[1].str();

        events.push_back(std::move(ev));
    }
    return events;
}

std::vector<SmfEvent> NwdafCollector::collectSmfEvents() {
    std::vector<SmfEvent> events;
    auto service_it = config_.nf_service_names.find("SMF");
    if (service_it == config_.nf_service_names.end()) return events;

    auto lines = readJournalLines(service_it->second, config_.smf_journal_lines);
    std::regex supi_re(config_.supi_regex);

    for (const auto& line : lines) {
        SmfEvent ev;
        ev.raw_line = line;
        ev.timestamp_iso = nowISO();

        if (line.find("PDU Session Establishment") != std::string::npos ||
            line.find("pdu session establish") != std::string::npos ||
            line.find("[Established]") != std::string::npos)
            ev.event_type = "PDU_ESTABLISHED";
        else if (line.find("PDU Session Release") != std::string::npos ||
                 line.find("pdu session release") != std::string::npos ||
                 line.find("[Released]") != std::string::npos ||
                 line.find("[Removed]") != std::string::npos)   // ARCH-04: Open5GS v2.7.6
            ev.event_type = "PDU_RELEASED";
        else if (line.find("QoS Flow") != std::string::npos ||
                 line.find("qos flow") != std::string::npos)
            ev.event_type = "QOS_FLOW_CHANGE";
        else
            continue;

        std::smatch m;
        if (std::regex_search(line, m, supi_re)) {
            ev.supi       = "imsi-" + m[1].str();
            ev.session_id = ev.supi;   // ARCH-04: use SUPI as session key
        }

        events.push_back(std::move(ev));
    }
    return events;
}

// PROD-08: Pre-snapshot approach — no blocking sleep.
// On the first call we just store a baseline; subsequent calls compute the
// delta over the actual elapsed collection interval, matching accuracy to
// collection_interval_seconds instead of a fixed 1s window.
ThroughputSample NwdafCollector::collectUPFThroughput() {
    ThroughputSample s;
    s.timestamp_iso = nowISO();
    s.total_dl_bps = 0; s.total_ul_bps = 0;
    auto now = std::chrono::steady_clock::now();

    std::map<std::string, std::pair<uint64_t,uint64_t>> current;
    for (const auto& iface : config_.throughput_interfaces) {
        auto stats = readNetStats(iface);
        if (stats.first == 0 && stats.second == 0) continue;
        current[iface] = stats;
    }

    if (has_net_snapshot_) {
        double elapsed_s = std::chrono::duration<double>(
            now - prev_net_snapshot_.ts).count();
        if (elapsed_s > 0.01) {
            for (const auto& [iface, cur] : current) {
                auto prev_it = prev_net_snapshot_.readings.find(iface);
                if (prev_it == prev_net_snapshot_.readings.end()) continue;
                double rx_bps = (cur.first  - prev_it->second.first)  * 8.0 / elapsed_s;
                double tx_bps = (cur.second - prev_it->second.second) * 8.0 / elapsed_s;
                s.per_iface[iface] = {rx_bps, tx_bps};
                s.total_dl_bps += rx_bps;
                s.total_ul_bps += tx_bps;
            }
        }
    }
    prev_net_snapshot_ = {current, now};
    has_net_snapshot_ = true;
    s.total_dl_kbps = s.total_dl_bps / 1000.0;
    s.total_ul_kbps = s.total_ul_bps / 1000.0;
    return s;
}

std::vector<NfMetric> NwdafCollector::collectNfLoad() {
    std::vector<NfMetric> metrics;

    for (const auto& [nf_type, service_name] : config_.nf_service_names) {
        NfMetric m;
        m.nf_type = nf_type;
        m.pid = 0;
        m.cpu_seconds = 0;
        m.mem_kb = 0;
        m.load_pct = 0;

        // Check systemctl is-active
        std::string check_cmd = "systemctl is-active open5gs-" + service_name + " 2>/dev/null";
        FILE* fp = popen(check_cmd.c_str(), "r");
        char buf[64] = {};
        if (fp) {
            fgets(buf, sizeof(buf), fp);
            pclose(fp);
        }
        std::string status_str(buf);
        if (!status_str.empty() && status_str.back() == '\n') status_str.pop_back();

        if (status_str == "active")        m.status = "active";
        else if (status_str == "inactive") m.status = "inactive";
        else if (status_str == "failed")   m.status = "failed";
        else                               m.status = "unknown";

        // Get MainPID
        std::string pid_cmd = "systemctl show open5gs-" + service_name +
                              " --property=MainPID --value 2>/dev/null";
        FILE* fp2 = popen(pid_cmd.c_str(), "r");
        char pid_buf[32] = {};
        if (fp2) { fgets(pid_buf, sizeof(pid_buf), fp2); pclose(fp2); }
        try { m.pid = std::stoi(std::string(pid_buf)); } catch (...) { m.pid = 0; }

        if (m.pid > 0 && m.status == "active") {
            auto [utime, stime] = readProcStat(m.pid);
            long sc = sysconf(_SC_CLK_TCK);
            if (sc <= 0) sc = 100;
            m.cpu_seconds = (double)(utime + stime) / (double)sc;  // cumulative, for nfCpuUsage
            m.mem_kb = readProcMemKb(m.pid);
            m.load_pct = computeCpuPct(m.pid);  // BUG-01: instantaneous rate, not cumulative
        } else {
            m.load_pct = 0.0;
        }
        if      (m.load_pct >= 60) m.load_label = "HIGH";
        else if (m.load_pct >= 20) m.load_label = "MEDIUM";
        else                       m.load_label = "LOW";

        metrics.push_back(std::move(m));
    }
    return metrics;
}

int NwdafCollector::querySubscriberCountFromMongo() {
#ifdef NWDAF_HAS_MONGODB
    if (mongo_client_) {
        try {
            auto db = (*mongo_client_)[config_.mongodb_db];
            auto coll = db["subscribers"];
            return (int)coll.count_documents({});
        } catch (...) {}
    }
#endif
    // Fallback: query via mongosh — works even when libmongocxx is not compiled in.
    FILE* fp = popen(
        "mongosh --quiet open5gs --eval 'db.subscribers.countDocuments({})' 2>/dev/null",
        "r");
    if (!fp) return 0;
    char buf[32] = {};
    int count = 0;
    if (fgets(buf, sizeof(buf), fp)) {
        try { count = std::stoi(std::string(buf)); } catch (...) {}
    }
    pclose(fp);
    return count;
}

int NwdafCollector::getSubscriberCount() {
    return querySubscriberCountFromMongo();
}

// ── PROD-01: SQLite throughput history persistence ────────────────────────────

#ifdef NWDAF_HAS_SQLITE
void NwdafCollector::initHistoryDb() {
    if (config_.history_backend != "sqlite" || config_.history_db_path.empty()) return;
    if (sqlite3_open(config_.history_db_path.c_str(), &history_db_) != SQLITE_OK) {
        spdlog::warn("PROD-01: Cannot open history DB {}: {}",
                     config_.history_db_path, sqlite3_errmsg(history_db_));
        sqlite3_close(history_db_);
        history_db_ = nullptr;
        return;
    }
    const char* ddl =
        "CREATE TABLE IF NOT EXISTS throughput_history ("
        "  ts TEXT PRIMARY KEY,"
        "  dl_bps REAL, ul_bps REAL, dl_kbps REAL, ul_kbps REAL"
        ");";
    char* err = nullptr;
    if (sqlite3_exec(history_db_, ddl, nullptr, nullptr, &err) != SQLITE_OK) {
        spdlog::warn("PROD-01: DDL error: {}", err);
        sqlite3_free(err);
    }
    spdlog::info("PROD-01: throughput history DB opened at {}", config_.history_db_path);
}

void NwdafCollector::persistThroughputSample(const ThroughputSample& s) {
    if (!history_db_) return;
    const char* sql =
        "INSERT OR REPLACE INTO throughput_history (ts, dl_bps, ul_bps, dl_kbps, ul_kbps)"
        " VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(history_db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, s.timestamp_iso.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, s.total_dl_bps);
    sqlite3_bind_double(stmt, 3, s.total_ul_bps);
    sqlite3_bind_double(stmt, 4, s.total_dl_kbps);
    sqlite3_bind_double(stmt, 5, s.total_ul_kbps);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // Keep only the last N rows to match in-memory history size
    const char* trim_sql =
        "DELETE FROM throughput_history WHERE ts NOT IN ("
        "  SELECT ts FROM throughput_history ORDER BY ts DESC LIMIT ?"
        ");";
    sqlite3_stmt* trim = nullptr;
    if (sqlite3_prepare_v2(history_db_, trim_sql, -1, &trim, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(trim, 1, config_.throughput_history_size);
        sqlite3_step(trim);
        sqlite3_finalize(trim);
    }
}

void NwdafCollector::warmFromHistoryDb() {
    if (!history_db_) return;
    const char* sql =
        "SELECT ts, dl_bps, ul_bps, dl_kbps, ul_kbps"
        " FROM throughput_history ORDER BY ts ASC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(history_db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int(stmt, 1, config_.throughput_history_size);

    std::lock_guard<std::mutex> lk(mutex_);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ThroughputSample s;
        const char* ts = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        s.timestamp_iso  = ts ? ts : "";
        s.total_dl_bps   = sqlite3_column_double(stmt, 1);
        s.total_ul_bps   = sqlite3_column_double(stmt, 2);
        s.total_dl_kbps  = sqlite3_column_double(stmt, 3);
        s.total_ul_kbps  = sqlite3_column_double(stmt, 4);
        throughput_history_.push_back(s);
        // Also warm EWMA with historical data
        dl_ewma_.update(s.total_dl_kbps);
        ul_ewma_.update(s.total_ul_kbps);
    }
    sqlite3_finalize(stmt);
    spdlog::info("PROD-01: warmed {} throughput samples from SQLite history",
                 throughput_history_.size());
}
#endif  // NWDAF_HAS_SQLITE

// ── Background thread ─────────────────────────────────────────────────────────

void NwdafCollector::startBackgroundCollection() {
    if (running_) return;
    running_ = true;
    // PROD-01: warm the in-memory deque from persisted history before the first
    // bgLoop iteration so ABNORMAL_BEHAVIOUR doesn't return INSUFFICIENT_DATA
    // immediately after a restart.
    warmFromHistoryDb();
    bg_thread_ = std::thread(&NwdafCollector::bgLoop, this);
}

void NwdafCollector::stopBackgroundCollection() {
    running_ = false;
    if (bg_thread_.joinable()) bg_thread_.join();
}

void NwdafCollector::bgLoop() {
    while (running_) {
        try {
            auto tp = collectUPFThroughput();
            auto amf = collectAmfEvents();
            auto smf = collectSmfEvents();
            auto nf  = collectNfLoad();

            {
                std::lock_guard<std::mutex> lk(mutex_);
                throughput_history_.push_back(tp);
                while ((int)throughput_history_.size() > config_.throughput_history_size)
                    throughput_history_.pop_front();
                for (auto& e : amf) {
                    amf_events_.push_back(e);
                    if ((int)amf_events_.size() > 1000) amf_events_.pop_front();
                }
                for (auto& e : smf) {
                    smf_events_.push_back(e);
                    if ((int)smf_events_.size() > 1000) smf_events_.pop_front();
                    // ARCH-04: maintain stateful PDU session set so the count
                    // survives log rotation and service restarts.
                    if (!e.session_id.empty()) {
                        if (e.event_type == "PDU_ESTABLISHED")
                            active_sessions_.insert(e.session_id);
                        else if (e.event_type == "PDU_RELEASED")
                            active_sessions_.erase(e.session_id);
                    }
                }
                nf_metrics_ = nf;
                // BUG-02: update EWMA exactly once per collection interval here,
                // never in the analytics read path.
                dl_ewma_.update(tp.total_dl_kbps);
                ul_ewma_.update(tp.total_ul_kbps);
            }
#ifdef NWDAF_HAS_SQLITE
            // PROD-01: persist sample outside the mutex
            persistThroughputSample(tp);
#endif
        } catch (const std::exception& e) {
            spdlog::error("Background collection error: {}", e.what());
        }

        // Sleep in 1-second increments to allow clean shutdown
        for (int i = 0; i < config_.collection_interval_seconds && running_; ++i)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// PROD-04: hot-reload support — safe to call from SIGHUP handler
void NwdafCollector::updateConfig(int collection_interval_seconds, double ewma_alpha) {
    config_.collection_interval_seconds = collection_interval_seconds;
    std::lock_guard<std::mutex> lk(mutex_);
    dl_ewma_.setAlpha(ewma_alpha);
    ul_ewma_.setAlpha(ewma_alpha);
}

// ── BUG-02: EWMA prediction accessors ────────────────────────────────────────

double NwdafCollector::getDlEwmaPrediction() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return dl_ewma_.predict();
}

double NwdafCollector::getUlEwmaPrediction() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return ul_ewma_.predict();
}

// ── ARCH-04: stateful PDU session count ──────────────────────────────────────

// Read the live UPF session count from the Prometheus scrape endpoint.
// Returns -1 on failure so callers can fall back to the event-tracker.
static int readUpfSessionCountFromPrometheus() {
    FILE* fp = popen(
        "curl -s --connect-timeout 2 http://127.0.0.7:9090/metrics 2>/dev/null"
        " | grep -v '^#' | grep 'upf_sessionnbr'", "r");
    if (!fp) return -1;
    char buf[256] = {};
    int count = -1;
    while (fgets(buf, sizeof(buf), fp)) {
        std::string line(buf);
        auto pos = line.rfind(' ');
        if (pos != std::string::npos) {
            try { count = std::stoi(line.substr(pos + 1)); } catch (...) {}
        }
    }
    pclose(fp);
    return count;
}

int NwdafCollector::getActivePduSessionCount() const {
    // Prefer the live UPF Prometheus metric; the event-tracker misses sessions
    // that were not cleanly released (e.g. pkill nr-ue without NAS deregistration).
    int upf_count = readUpfSessionCountFromPrometheus();
    if (upf_count >= 0) return upf_count;
    std::lock_guard<std::mutex> lk(mutex_);
    return (int)active_sessions_.size();
}

// ── Thread-safe accessors ─────────────────────────────────────────────────────

std::vector<AmfEvent> NwdafCollector::getRecentAmfEvents(int n) const {
    std::lock_guard<std::mutex> lk(mutex_);
    int count = std::min(n, (int)amf_events_.size());
    return {amf_events_.end() - count, amf_events_.end()};
}

std::vector<SmfEvent> NwdafCollector::getRecentSmfEvents(int n) const {
    std::lock_guard<std::mutex> lk(mutex_);
    int count = std::min(n, (int)smf_events_.size());
    return {smf_events_.end() - count, smf_events_.end()};
}

std::vector<ThroughputSample> NwdafCollector::getThroughputHistory(int n) const {
    std::lock_guard<std::mutex> lk(mutex_);
    int count = std::min(n, (int)throughput_history_.size());
    return {throughput_history_.end() - count, throughput_history_.end()};
}

std::vector<NfMetric> NwdafCollector::getCachedNfMetrics() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return nf_metrics_;
}
