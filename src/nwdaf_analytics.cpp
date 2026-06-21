#include "nwdaf_analytics.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <filesystem>
#include <unistd.h>

const std::set<std::string> NwdafAnalyticsEngine::VALID_ANALYTICS_IDS = {
    "NF_LOAD", "UE_MOBILITY", "UE_COMMUNICATION",
    "ABNORMAL_BEHAVIOUR", "QoS_SUSTAINABILITY",
    "SERVICE_EXPERIENCE", "NETWORK_PERFORMANCE"
};

NwdafAnalyticsEngine::NwdafAnalyticsEngine(NwdafCollector& collector,
                                           const NwdafConfig& config)
    : collector_(collector),
      config_(config),
      anomaly_model_(100, config.anomaly_contamination, 
                     config.anomaly_seed ? config.anomaly_seed : std::random_device{}())
    // BUG-02: dl_ewma_ / ul_ewma_ moved to NwdafCollector — updated in bgLoop only
{
    loadModels();
}

std::string NwdafAnalyticsEngine::nowISO() const {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    gmtime_r(&t, &tm_buf);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return buf;
}

// ── ARCH-01: data-quality-driven confidence ───────────────────────────────────

double NwdafAnalyticsEngine::computeConfidence(int data_points, int min_points,
                                               int max_points,
                                               double baseline_quality)
{
    if (data_points < min_points) return 0.0;
    double coverage = std::min(1.0, (double)data_points / (double)max_points);
    return std::round(baseline_quality * coverage * 95.0);  // max 95% for any model
}

// ── COMP-04: time window helpers ──────────────────────────────────────────────

std::chrono::system_clock::time_point NwdafAnalyticsEngine::parseISO(const std::string& ts) {
    struct tm tm_buf = {};
    // Accept "2024-04-21T10:00:00Z" (UTC suffix)
    const char* end = strptime(ts.c_str(), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    if (!end) strptime(ts.c_str(), "%Y-%m-%dT%H:%M:%S", &tm_buf);
    return std::chrono::system_clock::from_time_t(timegm(&tm_buf));
}

bool NwdafAnalyticsEngine::inWindow(const std::string& event_ts,
                                    const std::string& start_ts,
                                    const std::string& end_ts) {
    if (start_ts.empty() && end_ts.empty()) return true;
    if (event_ts.empty()) return true; // no timestamp on event — include it
    auto ev_tp = parseISO(event_ts);
    if (!start_ts.empty() && ev_tp < parseISO(start_ts)) return false;
    if (!end_ts.empty()   && ev_tp > parseISO(end_ts))   return false;
    return true;
}

std::vector<ThroughputSample> NwdafAnalyticsEngine::filterByWindow(
    const std::vector<ThroughputSample>& hist,
    const std::string& start_ts,
    const std::string& end_ts)
{
    if (start_ts.empty() && end_ts.empty()) return hist;
    std::vector<ThroughputSample> out;
    for (const auto& s : hist)
        if (inWindow(s.timestamp_iso, start_ts, end_ts)) out.push_back(s);
    return out;
}

std::vector<AmfEvent> NwdafAnalyticsEngine::filterAmfByWindow(
    const std::vector<AmfEvent>& events,
    const std::string& start_ts,
    const std::string& end_ts)
{
    if (start_ts.empty() && end_ts.empty()) return events;
    std::vector<AmfEvent> out;
    for (const auto& e : events)
        if (inWindow(e.timestamp_iso, start_ts, end_ts)) out.push_back(e);
    return out;
}

std::vector<SmfEvent> NwdafAnalyticsEngine::filterSmfByWindow(
    const std::vector<SmfEvent>& events,
    const std::string& start_ts,
    const std::string& end_ts)
{
    if (start_ts.empty() && end_ts.empty()) return events;
    std::vector<SmfEvent> out;
    for (const auto& e : events)
        if (inWindow(e.timestamp_iso, start_ts, end_ts)) out.push_back(e);
    return out;
}

// ── COMP-03: SUPI filter helpers ──────────────────────────────────────────────

std::vector<AmfEvent> NwdafAnalyticsEngine::filterAmfBySupi(
    const std::vector<AmfEvent>& events, const std::string& supi)
{
    if (supi.empty()) return events;
    std::vector<AmfEvent> out;
    std::copy_if(events.begin(), events.end(), std::back_inserter(out),
                 [&](const AmfEvent& e){ return e.supi == supi; });
    return out;
}

std::vector<SmfEvent> NwdafAnalyticsEngine::filterSmfBySupi(
    const std::vector<SmfEvent>& events, const std::string& supi)
{
    if (supi.empty()) return events;
    std::vector<SmfEvent> out;
    std::copy_if(events.begin(), events.end(), std::back_inserter(out),
                 [&](const SmfEvent& e){ return e.supi == supi; });
    return out;
}

void NwdafAnalyticsEngine::loadModels() {
    std::string path = config_.model_dir + "/isolation_forest.json";
    try {
        anomaly_model_.load(path);
        spdlog::info("Loaded anomaly model from {}", path);
    } catch (...) {
        // ARCH-02: dimension mismatch (old 2-D file) also lands here — safe to ignore
        spdlog::info("No compatible anomaly model found — operating in rule-based mode");
    }
}

void NwdafAnalyticsEngine::saveModels() {
    // PROD-07: write-then-rename so a mid-write crash never leaves a corrupt file.
    std::string final_path = config_.model_dir + "/isolation_forest.json";
    std::string tmp_path   = final_path + ".tmp." + std::to_string(getpid());
    try {
        std::filesystem::create_directories(config_.model_dir);
        anomaly_model_.save(tmp_path);
        std::filesystem::rename(tmp_path, final_path);
        spdlog::info("Saved anomaly model to {}", final_path);
    } catch (const std::exception& e) {
        spdlog::warn("Failed to save anomaly model: {}", e.what());
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
    }
}

// PROD-03: accessor methods for the Prometheus metrics endpoint
std::pair<double,double> NwdafAnalyticsEngine::getCurrentThroughput() const {
    auto hist = collector_.getThroughputHistory(1);
    if (hist.empty()) return {0.0, 0.0};
    return {hist.back().total_dl_kbps, hist.back().total_ul_kbps};
}

std::vector<NfMetric> NwdafAnalyticsEngine::getCurrentNfMetrics() const {
    return collector_.getCachedNfMetrics();
}

// PROD-04: hot-reload — update contamination; takes effect on the next retrain()
void NwdafAnalyticsEngine::updateConfig(double anomaly_contamination) {
    config_.anomaly_contamination = anomaly_contamination;
}

bool NwdafAnalyticsEngine::isReady() const {
    std::shared_lock<std::shared_mutex> lock(ml_mutex_);
    return anomaly_model_.isFitted();
}

// BUG-04: explicit retrain — always calls fit(), regardless of isFitted() state
json NwdafAnalyticsEngine::retrain() {
    auto hist = collector_.getThroughputHistory(360);
    int data_points = (int)hist.size();

    // Quality gate: require minimum sample count (~20 min at 10s interval)
    int min_samples = config_.anomaly_min_samples;
    double min_variance_kbps = config_.baseline_stddev_min_kbps;

    if (data_points < min_samples) {
        return {
            {"status",     "INSUFFICIENT_DATA"},
            {"dataPoints", data_points},
            {"required",   min_samples},
            {"message",    "Collect more traffic data before training"}
        };
    }

    // Quality gate: require at least some traffic variation
    std::vector<double> dl_vals, ul_vals;
    for (const auto& s : hist) {
        dl_vals.push_back(s.total_dl_kbps);
        ul_vals.push_back(s.total_ul_kbps);
    }
    auto stddev = [](const std::vector<double>& v) {
        double mean = std::accumulate(v.begin(), v.end(), 0.0) / v.size();
        double var = 0;
        for (double x : v) var += (x - mean) * (x - mean);
        return std::sqrt(var / v.size());
    };
    double dl_std = stddev(dl_vals);
    double ul_std = stddev(ul_vals);
    if (dl_std < min_variance_kbps && ul_std < min_variance_kbps) {
        return {
            {"status",  "BASELINE_TOO_LOW"},
            {"message", "Insufficient traffic variance to train anomaly model"},
            {"dl_std",  dl_std},
            {"ul_std",  ul_std},
            {"required", min_variance_kbps}
        };
    }

    if (data_points < config_.anomaly_min_samples) {
        return {
            {"status",     "skipped"},
            {"reason",     "INSUFFICIENT_DATA"},
            {"dataPoints", data_points}
        };
    }

    // ARCH-02: build 5-feature training vectors
    // Context features are current snapshots — same value across all training
    // samples, providing a reference for the current operating point.
    auto nf_metrics = collector_.getCachedNfMetrics();
    double nf_load_avg = 0.0;
    if (!nf_metrics.empty()) {
        for (const auto& m : nf_metrics) nf_load_avg += m.load_pct;
        nf_load_avg /= (double)nf_metrics.size();
    }

    int active_sessions = collector_.getActivePduSessionCount();

    auto amf_events = collector_.getRecentAmfEvents(500);
    int auth_failures = 0;
    for (const auto& e : amf_events)
        if (e.event_type == "AUTH_FAILURE") ++auth_failures;
    double window_minutes = (double)hist.size() * config_.collection_interval_seconds / 60.0;
    double auth_failure_rate = (window_minutes > 0) ? auth_failures / window_minutes : 0.0;

    std::vector<std::array<double,5>> X;
    X.reserve(hist.size());
    for (const auto& s : hist)
        X.push_back({s.total_dl_kbps, s.total_ul_kbps,
                     nf_load_avg, (double)active_sessions, auth_failure_rate});

    {
        std::unique_lock<std::shared_mutex> lk(ml_mutex_);
        anomaly_model_.fit(X);
    }
    saveModels();

    return {
        {"status",     "trained"},
        {"dataPoints", (int)X.size()},
        {"nFeatures",  5},
        {"ts",         nowISO()}
    };
}

json NwdafAnalyticsEngine::compute(const std::string& analytics_id,
                                   const std::string& supi,
                                   const std::string& start_ts,
                                   const std::string& end_ts)
{
    if (analytics_id == "NF_LOAD")             return nfLoad(supi, start_ts, end_ts);
    if (analytics_id == "UE_MOBILITY")         return ueMobility(supi, start_ts, end_ts);
    if (analytics_id == "UE_COMMUNICATION")    return ueCommunication(supi, start_ts, end_ts);
    if (analytics_id == "ABNORMAL_BEHAVIOUR")  return abnormalBehaviour(supi, start_ts, end_ts);
    if (analytics_id == "QoS_SUSTAINABILITY")  return qosSustainability(supi, start_ts, end_ts);
    if (analytics_id == "SERVICE_EXPERIENCE")  return serviceExperience(supi, start_ts, end_ts);
    if (analytics_id == "NETWORK_PERFORMANCE") return networkPerformance(supi, start_ts, end_ts);
    throw std::invalid_argument("Unknown analyticsId: " + analytics_id);
}

// ── NF_LOAD ───────────────────────────────────────────────────────────────────

json NwdafAnalyticsEngine::nfLoad(const std::string&,
                                   const std::string&,
                                   const std::string&) {
    auto metrics = collector_.getCachedNfMetrics();
    if (metrics.empty()) metrics = collector_.collectNfLoad();

    json load_list = json::array();
    std::vector<std::string> overloaded;

    // ARCH-01: count active NFs for data-quality-driven confidence
    int total_nf  = (int)metrics.size();
    int active_nf = 0;

    for (const auto& m : metrics) {
        json entry = {
            {"nfType",   m.nf_type},
            {"nfStatus", m.status},
            {"nfLoadLevelInfo", {
                {"nfLoadLevel",      m.load_pct / 100.0},
                {"nfLoadLevelLabel", m.load_label},
                {"nfCpuUsage",       m.cpu_seconds},
                {"nfMemoryUsage",    m.mem_kb}
            }}
        };
        load_list.push_back(entry);
        if (m.load_label == "OVERLOADED") overloaded.push_back(m.nf_type);
        if (m.status == "active") ++active_nf;
    }

    std::string recommendation = overloaded.empty() ? "STABLE" : "SCALE_OUT";

    // ARCH-01: confidence driven by fraction of active NFs; degraded 15% when
    // overloads are present because predictions are less reliable under stress.
    int confidence = metrics.empty() ? 0
        : (int)computeConfidence(active_nf, 1, total_nf,
                                 overloaded.empty() ? 1.0 : 0.85);

    // Derive nfHealthSummary from max load level across all NFs
    double max_load = 0.0;
    for (const auto& m : metrics) max_load = std::max(max_load, m.load_pct);
    std::string nf_health_summary = max_load >= 60.0 ? "CRITICAL"
                                  : max_load >= 20.0 ? "DEGRADED"
                                  : "HEALTHY";

    return {
        {"analyticsId",    "NF_LOAD"},
        {"ts",             nowISO()},
        {"nfLoadLevelList", load_list},
        {"overloadedNfs",  overloaded},
        {"recommendation", recommendation},
        {"nfHealthSummary", nf_health_summary},
        {"confidence",     confidence}
    };
}

// ── UE_MOBILITY ───────────────────────────────────────────────────────────────

json NwdafAnalyticsEngine::ueMobility(const std::string& supi,
                                       const std::string& start_ts,
                                       const std::string& end_ts) {
    auto events = filterAmfByWindow(collector_.getRecentAmfEvents(500), start_ts, end_ts);
    events = filterAmfBySupi(events, supi);

    int reg = 0, dereg = 0, handover = 0, auth_fail = 0;
    for (const auto& e : events) {
        if      (e.event_type == "REGISTRATION")   ++reg;
        else if (e.event_type == "DEREGISTRATION") ++dereg;
        else if (e.event_type == "HANDOVER")        ++handover;
        else if (e.event_type == "AUTH_FAILURE")    ++auth_fail;
    }

    std::string pattern = (reg + handover) > 20 ? "HIGH" : "LOW";

    return {
        {"analyticsId",        "UE_MOBILITY"},
        {"ts",                 nowISO()},
        {"supi",               supi.empty() ? "ALL" : supi},
        {"registrationCount",  reg},
        {"deregistrationCount",dereg},
        {"handoverCount",      handover},
        {"authFailureCount",   auth_fail},
        {"mobilityPattern",    pattern},
        {"confidence",         85}
    };
}

// ── UE_COMMUNICATION ─────────────────────────────────────────────────────────

json NwdafAnalyticsEngine::ueCommunication(const std::string& supi,
                                            const std::string& start_ts,
                                            const std::string& end_ts) {
    // COMP-03: filter by SUPI; COMP-04: filter by time window
    auto smf = filterSmfByWindow(collector_.getRecentSmfEvents(500), start_ts, end_ts);
    smf = filterSmfBySupi(smf, supi);

    int established = 0, released = 0;
    for (const auto& e : smf) {
        if      (e.event_type == "PDU_ESTABLISHED") ++established;
        else if (e.event_type == "PDU_RELEASED")    ++released;
    }
    int active = std::max(0, established - released);

    auto hist = filterByWindow(collector_.getThroughputHistory(1), start_ts, end_ts);
    double dl_kbps = hist.empty() ? 0.0 : hist.back().total_dl_kbps;
    double ul_kbps = hist.empty() ? 0.0 : hist.back().total_ul_kbps;

    int subscribers = supi.empty() ? collector_.getSubscriberCount()
                                   : (!smf.empty() ? 1 : 0);

    json result = {
        {"analyticsId",      "UE_COMMUNICATION"},
        {"ts",               nowISO()},
        {"pduSessionEstCount", established},
        {"pduSessionRelCount", released},
        {"activePduSessions",  active},
        {"totalSubscribers",   subscribers},
        {"currentDlKbps",      dl_kbps},
        {"currentUlKbps",      ul_kbps},
        {"confidence",         88}
    };
    if (!supi.empty()) result["supi"] = supi;
    return result;
}

// ── ABNORMAL_BEHAVIOUR ────────────────────────────────────────────────────────

json NwdafAnalyticsEngine::abnormalBehaviour(const std::string& supi,
                                              const std::string& start_ts,
                                              const std::string& end_ts) {
    // COMP-03: per-UE mode — use SMF event pattern analysis when SUPI is specified.
    if (!supi.empty()) {
        auto smf = filterSmfByWindow(collector_.getRecentSmfEvents(500), start_ts, end_ts);
        smf = filterSmfBySupi(smf, supi);
        int est = 0, rel = 0;
        for (const auto& e : smf) {
            if      (e.event_type == "PDU_ESTABLISHED") ++est;
            else if (e.event_type == "PDU_RELEASED")    ++rel;
        }
        bool anomaly = (est > 10) || (est > 0 && rel == 0 && est > 3);
        return {
            {"analyticsId",    "ABNORMAL_BEHAVIOUR"},
            {"ts",             nowISO()},
            {"supi",           supi},
            {"anomalyDetected", anomaly},
            {"anomalyType",    anomaly ? "UNEXPECTED_WAKEUP" : "NONE"},
            {"pduEstCount",    est},
            {"pduRelCount",    rel},
            {"dataPoints",     (int)smf.size()},
            {"note",           "Per-UE mode: SMF event heuristic "
                               "(aggregate throughput not decomposable per gtp5g constraint)"},
            {"confidence",     65}
        };
    }

    auto hist = filterByWindow(collector_.getThroughputHistory(360), start_ts, end_ts);
    int n = (int)hist.size();

    if (n < config_.anomaly_min_samples) {
        return {
            {"analyticsId",   "ABNORMAL_BEHAVIOUR"},
            {"ts",            nowISO()},
            {"anomalyDetected", false},
            {"reason",        "INSUFFICIENT_DATA"},
            {"dataPoints",    n},
            {"confidence",    0}
        };
    }

    // Compute DL/UL stddev
    std::vector<double> dl_vals, ul_vals;
    for (const auto& s : hist) {
        dl_vals.push_back(s.total_dl_kbps);
        ul_vals.push_back(s.total_ul_kbps);
    }
    auto stddev = [](const std::vector<double>& v) {
        double mean = std::accumulate(v.begin(), v.end(), 0.0) / v.size();
        double var = 0;
        for (double x : v) var += (x - mean) * (x - mean);
        return std::sqrt(var / v.size());
    };
    double dl_std = stddev(dl_vals);
    double ul_std = stddev(ul_vals);

    if (dl_std < config_.baseline_stddev_min_kbps && ul_std < config_.baseline_stddev_min_kbps) {
        return {
            {"analyticsId",    "ABNORMAL_BEHAVIOUR"},
            {"ts",             nowISO()},
            {"anomalyDetected", false},
            {"reason",         "BASELINE_TOO_LOW"},
            {"baselineDlStd",  dl_std},
            {"dataPoints",     n},
            {"confidence",     0}
        };
    }

    // ARCH-02: build 5-feature matrix — adds NF load, session count, and auth
    // failure rate to the per-sample throughput features.
    auto nf_metrics = collector_.getCachedNfMetrics();
    double nf_load_avg = 0.0;
    if (!nf_metrics.empty()) {
        for (const auto& m : nf_metrics) nf_load_avg += m.load_pct;
        nf_load_avg /= (double)nf_metrics.size();
    }

    int active_sessions = collector_.getActivePduSessionCount();

    auto amf_events = collector_.getRecentAmfEvents(500);
    int auth_failures = 0;
    for (const auto& e : amf_events)
        if (e.event_type == "AUTH_FAILURE") ++auth_failures;
    double window_minutes = (double)n * config_.collection_interval_seconds / 60.0;
    double auth_failure_rate = (window_minutes > 0) ? auth_failures / window_minutes : 0.0;

    std::vector<std::array<double,5>> X;
    X.reserve(n);
    for (const auto& s : hist)
        X.push_back({s.total_dl_kbps, s.total_ul_kbps,
                     nf_load_avg, (double)active_sessions, auth_failure_rate});

    // BUG-03: exclusive lock for fit; shared lock for predict/score
    bool just_fitted = false;
    {
        std::unique_lock<std::shared_mutex> lk(ml_mutex_);
        if (!anomaly_model_.isFitted()) {
            anomaly_model_.fit(X);
            just_fitted = true;
        }
    }
    if (just_fitted) saveModels();

    std::vector<int> preds;
    std::vector<double> scores;
    {
        std::shared_lock<std::shared_mutex> lk(ml_mutex_);
        preds  = anomaly_model_.predict(X);
        scores = anomaly_model_.scoresSamples(X);
    }

    std::vector<int> anomaly_indices;
    double score_sum = 0;
    for (int i = 0; i < n; ++i) {
        if (preds[i] == -1) {
            anomaly_indices.push_back(i);
            score_sum += scores[i];
        }
    }

    double anomaly_pct = 100.0 * anomaly_indices.size() / n;
    double avg_score = anomaly_indices.empty() ? 0.0 : score_sum / anomaly_indices.size();

    std::string anomaly_type = "SUSPICION_OF_DDOS_ATTACK";
    if      (dl_std > 50)      anomaly_type = "UNEXPECTED_LARGE_RATE";
    else if (anomaly_pct > 20) anomaly_type = "UNEXPECTED_WAKEUP";

    // ARCH-01: confidence driven by data coverage; penalised when model is not
    // yet fitted from a warm start (uses rule-based threshold instead of trained one).
    int confidence = (int)computeConfidence(n, config_.anomaly_min_samples, 360,
                                            anomaly_model_.isFitted() ? 1.0 : 0.7);

    return {
        {"analyticsId",    "ABNORMAL_BEHAVIOUR"},
        {"ts",             nowISO()},
        {"anomalyDetected", !anomaly_indices.empty()},
        {"anomalyPct",     anomaly_pct},
        {"anomalyType",    anomaly_type},
        {"anomalyIndices", anomaly_indices},
        {"avgAnomalyScore", avg_score},
        {"dataPoints",     n},
        {"baselineDlStd",  dl_std},
        {"confidence",     confidence}
    };
}

// ── QoS_SUSTAINABILITY ────────────────────────────────────────────────────────

json NwdafAnalyticsEngine::qosSustainability(const std::string& supi,
                                              const std::string& start_ts,
                                              const std::string& end_ts) {
    // BUG-02: EWMA state updated solely in NwdafCollector::bgLoop — read-only here.
    auto hist = filterByWindow(collector_.getThroughputHistory(1), start_ts, end_ts);
    double cur_dl = hist.empty() ? 0.0 : hist.back().total_dl_kbps;
    double cur_ul = hist.empty() ? 0.0 : hist.back().total_ul_kbps;

    double pred_dl = collector_.getDlEwmaPrediction();
    double pred_ul = collector_.getUlEwmaPrediction();

    auto trend = [](double cur, double pred) -> std::string {
        if (cur == 0) return "STABLE";
        double delta_pct = (pred - cur) / cur * 100.0;
        if      (delta_pct > 10)  return "INCREASING";
        else if (delta_pct < -10) return "DECREASING";
        else                      return "STABLE";
    };

    std::string dl_trend = trend(cur_dl, pred_dl);
    std::string ul_trend = trend(cur_ul, pred_ul);

    std::string violation_risk = "LOW";
    if      (pred_dl < 10)  violation_risk = "HIGH";
    else if (pred_dl < 50)  violation_risk = "MEDIUM";

    std::string sustainability = pred_dl > 5.0 ? "SUSTAINABLE" : "UNSUSTAINABLE";

    json result = {
        {"analyticsId",   "QoS_SUSTAINABILITY"},
        {"ts",            nowISO()},
        {"currentDlKbps", cur_dl},
        {"currentUlKbps", cur_ul},
        {"predictedDlKbps", pred_dl},
        {"predictedUlKbps", pred_ul},
        {"dlTrend",        dl_trend},
        {"ulTrend",        ul_trend},
        {"violationRisk",  violation_risk},
        {"sustainability", sustainability},
        {"confidence",     80}
    };

    // COMP-03: per-UE throughput unavailable (gtp5g network-wide only) — signal this
    if (!supi.empty()) {
        result["supi"]         = supi;
        result["supiFiltered"] = false;
        result["note"] = "Per-UE QoS prediction not available: throughput is measured "
                         "network-wide via /sys/class/net (gtp5g constraint); "
                         "returning aggregate EWMA.";
    }
    return result;
}

// ── SERVICE_EXPERIENCE ────────────────────────────────────────────────────────

json NwdafAnalyticsEngine::serviceExperience(const std::string& supi,
                                              const std::string& start_ts,
                                              const std::string& end_ts) {
    auto hist = filterByWindow(collector_.getThroughputHistory(1), start_ts, end_ts);
    double dl_kbps = hist.empty() ? 0.0 : hist.back().total_dl_kbps;
    double ul_kbps = hist.empty() ? 0.0 : hist.back().total_ul_kbps;

    double mos = 2.0;
    if      (dl_kbps > 1000) mos = 4.5;
    else if (dl_kbps > 500)  mos = 4.0;
    else if (dl_kbps > 100)  mos = 3.5;
    else if (dl_kbps > 50)   mos = 3.0;

    std::string category = "POOR";
    if      (mos > 4.0) category = "EXCELLENT";
    else if (mos > 3.5) category = "GOOD";
    else if (mos > 2.5) category = "FAIR";

    auto smf = filterSmfByWindow(collector_.getRecentSmfEvents(500), start_ts, end_ts);
    smf = filterSmfBySupi(smf, supi);

    int est = 0, rel = 0;
    for (const auto& e : smf) {
        if      (e.event_type == "PDU_ESTABLISHED") ++est;
        else if (e.event_type == "PDU_RELEASED")    ++rel;
    }
    int active = std::max(0, est - rel);
    int subscribers = supi.empty() ? collector_.getSubscriberCount()
                                   : (!smf.empty() ? 1 : 0);
    double ratio = (subscribers > 0) ? (double)active / subscribers : 0.0;

    json result = {
        {"analyticsId",      "SERVICE_EXPERIENCE"},
        {"ts",               nowISO()},
        {"mosScore",         mos},
        {"mosCategory",      category},
        {"dlKbps",           dl_kbps},
        {"ulKbps",           ul_kbps},
        {"activeSessionRatio", ratio},
        {"confidence",       75}
    };
    if (!supi.empty()) result["supi"] = supi;
    return result;
}

// ── NETWORK_PERFORMANCE ───────────────────────────────────────────────────────

json NwdafAnalyticsEngine::networkPerformance(const std::string&,
                                               const std::string& start_ts,
                                               const std::string& end_ts) {
    auto metrics = collector_.getCachedNfMetrics();
    if (metrics.empty()) metrics = collector_.collectNfLoad();

    int total_nf = (int)metrics.size();
    int active_nf = 0;
    for (const auto& m : metrics)
        if (m.status == "active") ++active_nf;

    double nf_health = total_nf > 0 ? 100.0 * active_nf / total_nf : 0.0;

    auto hist = filterByWindow(collector_.getThroughputHistory(1), start_ts, end_ts);
    double dl_kbps = hist.empty() ? 0.0 : hist.back().total_dl_kbps;

    // ARCH-04: use stateful session tracker instead of log-derived est-rel delta
    int active_sessions = collector_.getActivePduSessionCount();

    double dl_score  = std::min(100.0, dl_kbps / 10.0);
    double pdu_score = std::min(100.0, (double)active_sessions * 20.0);

    // ARCH-03: configurable weights from config (validated to sum to 1.0 at load time)
    double overall = config_.np_weight_nf_health * nf_health
                   + config_.np_weight_dl        * dl_score
                   + config_.np_weight_pdu       * pdu_score;

    std::string label = "POOR";
    if      (overall > 90) label = "EXCELLENT";
    else if (overall > 75) label = "GOOD";
    else if (overall > 50) label = "FAIR";

    std::string grade = overall >= 80 ? "A"
                      : overall >= 60 ? "B"
                      : overall >= 40 ? "C" : "D";

    return {
        {"analyticsId", "NETWORK_PERFORMANCE"},
        {"ts",          nowISO()},
        {"overallScore", overall},
        {"scoreLabel",   label},
        {"grade",        grade},
        {"components", {
            {"nfHealthScore", nf_health},
            {"dlScore",       dl_score},
            {"pduScore",      pdu_score}
        }},
        {"confidence", 82}
    };
}
