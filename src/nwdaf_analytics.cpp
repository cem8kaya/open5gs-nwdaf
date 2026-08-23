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
    "SERVICE_EXPERIENCE", "NETWORK_PERFORMANCE",
    // H1.4 — Rel-17/18 catalogue completion
    "SM_CONGESTION", "REDUNDANT_TRANSMISSION", "DISPERSION"
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
    // H1.4
    if (analytics_id == "SM_CONGESTION")          return smCongestion(supi, start_ts, end_ts);
    if (analytics_id == "REDUNDANT_TRANSMISSION") return redundantTransmission(supi, start_ts, end_ts);
    if (analytics_id == "DISPERSION")             return dispersion(supi, start_ts, end_ts);
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

    // H1.5: E-model-style MOS replaces the static DL-throughput step ladder.
    // Packet-loss and latency inputs stay unset until PFCP usage reporting
    // (H1.3) lands; the estimator applies zero impairment for absent signals
    // and degrades to the original ladder when no throughput sample exists.
    MosInputs mi;
    mi.has_throughput  = !hist.empty();
    mi.dl_kbps         = dl_kbps;
    mi.ul_kbps         = ul_kbps;
    mi.active_sessions = collector_.getActivePduSessionCount();

    MosResult mr = MosEstimator::estimate(mi);

    json result = {
        {"analyticsId",      "SERVICE_EXPERIENCE"},
        {"ts",               nowISO()},
        {"mosScore",         std::round(mr.mos * 100.0) / 100.0},
        {"mosCategory",      mr.category},
        {"mosMethod",        mr.method},
        {"rFactor",          std::round(mr.r_factor * 10.0) / 10.0},
        // Per-factor attribution — which impairment drove the score down.
        {"impairments", {
            {"throughput", std::round(mr.ie_throughput * 10.0) / 10.0},
            {"loss",       std::round((mr.ie_effective - mr.ie_throughput) * 10.0) / 10.0},
            {"delay",      std::round(mr.id_delay * 10.0) / 10.0}
        }},
        {"dlKbps",           dl_kbps},
        {"ulKbps",           ul_kbps},
        {"activeSessionRatio", ratio},
        {"confidence",       hist.empty() ? 40 : 78}
    };

    // Per-subscriber view: the aggregate pipe shared across active PDU sessions.
    // Reported alongside — not instead of — the aggregate score, so existing
    // consumers of mosScore keep their meaning.
    if (mr.per_session_available) {
        result["perSessionKbps"] = std::round(mr.per_session_kbps * 10.0) / 10.0;
        result["perSessionMos"]  = std::round(mr.per_session_mos * 100.0) / 100.0;
        result["activePduSessions"] = mi.active_sessions;
    }

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

// ── H1.4 DISPERSION helpers ──────────────────────────────────────────────────

// Gini coefficient over a non-negative series: 0 = perfectly even, → 1 as a
// single element takes the whole mass. Uses the sorted-rank formulation.
double NwdafAnalyticsEngine::giniCoefficient(std::vector<double> values) {
    if (values.size() < 2) return 0.0;
    for (auto& v : values) if (v < 0.0) v = 0.0;
    std::sort(values.begin(), values.end());
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    if (sum <= 0.0) return 0.0;
    const double n = (double)values.size();
    double weighted = 0.0;
    for (size_t i = 0; i < values.size(); ++i)
        weighted += (double)(i + 1) * values[i];
    double g = (2.0 * weighted) / (n * sum) - (n + 1.0) / n;
    return std::clamp(g, 0.0, 1.0);
}

double NwdafAnalyticsEngine::coefficientOfVariation(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    if (mean <= 0.0) return 0.0;
    double var = 0.0;
    for (double x : values) var += (x - mean) * (x - mean);
    return std::sqrt(var / values.size()) / mean;
}

namespace {
// Shared classifier for the dispersion/congestion bands below.
std::string bandOf(double v, double low, double high) {
    if (v >= high) return "HIGH";
    if (v >= low)  return "MEDIUM";
    return "LOW";
}
}  // namespace

// ── SM_CONGESTION (TS 23.288 §6.16 — SM congestion control experience) ───────
//
// NOTE ON SCOPE: the enhancement plan's H1.4 table pairs the `SM_CONGESTION`
// analytics ID with §6.8, but §6.8 is `USER_DATA_CONGESTION` — a distinct
// analytic keyed on user-plane congestion at a location. `SM_CONGESTION` is
// §6.16 and is about session-management congestion control (establishment
// rejects, back-off). This implementation follows §6.16, which is what the
// available SMF event data actually supports; `USER_DATA_CONGESTION` remains
// an open catalogue item because it needs per-location (TA/cell) input.

json NwdafAnalyticsEngine::smCongestion(const std::string& supi,
                                         const std::string& start_ts,
                                         const std::string& end_ts) {
    auto smf = filterSmfByWindow(collector_.getRecentSmfEvents(500), start_ts, end_ts);
    if (!supi.empty()) smf = filterSmfBySupi(smf, supi);

    int established = 0, failed = 0, released = 0;
    std::map<std::string, std::pair<int,int>> per_ue;  // supi → {attempts, failures}
    for (const auto& e : smf) {
        if (e.event_type == "PDU_ESTABLISHED") {
            ++established;
            if (!e.supi.empty()) { per_ue[e.supi].first++; }
        } else if (e.event_type == "PDU_EST_FAILED") {
            ++failed;
            if (!e.supi.empty()) { per_ue[e.supi].first++; per_ue[e.supi].second++; }
        } else if (e.event_type == "PDU_RELEASED") {
            ++released;
        }
    }

    int attempts = established + failed;
    double failure_ratio = attempts > 0 ? 100.0 * failed / attempts : 0.0;

    // Control-plane pressure: SMF and AMF load; user-plane pressure: UPF load.
    auto metrics = collector_.getCachedNfMetrics();
    if (metrics.empty()) metrics = collector_.collectNfLoad();
    double smf_load = 0.0, upf_load = 0.0, amf_load = 0.0;
    for (const auto& m : metrics) {
        if      (m.nf_type == "SMF") smf_load = m.load_pct;
        else if (m.nf_type == "UPF") upf_load = m.load_pct;
        else if (m.nf_type == "AMF") amf_load = m.load_pct;
    }
    double cp_load = std::max(smf_load, amf_load);

    // Congestion level: establishment failures dominate; NF load escalates a
    // level on its own once an NF is saturated, since rejects lag saturation.
    std::string level = "NONE";
    if      (failure_ratio >= 20.0 || cp_load >= 80.0 || upf_load >= 80.0) level = "HIGH";
    else if (failure_ratio >= 5.0  || cp_load >= 60.0 || upf_load >= 60.0) level = "MEDIUM";
    else if (failure_ratio > 0.0   || cp_load >= 40.0 || upf_load >= 40.0) level = "LOW";

    // TS 23.288 §6.16 smcceUeList — UEs bucketed by the congestion they saw.
    json high_ues = json::array(), med_ues = json::array(), low_ues = json::array();
    for (const auto& [ue, counts] : per_ue) {
        int ue_attempts = counts.first, ue_failures = counts.second;
        if (ue_failures == 0) continue;
        double ue_ratio = ue_attempts > 0 ? 100.0 * ue_failures / ue_attempts : 0.0;
        if      (ue_ratio >= 50.0) high_ues.push_back(ue);
        else if (ue_ratio >= 20.0) med_ues.push_back(ue);
        else                       low_ues.push_back(ue);
    }

    std::string recommendation = "NONE";
    if      (level == "HIGH")   recommendation = "APPLY_BACKOFF_AND_SCALE_OUT";
    else if (level == "MEDIUM") recommendation = "MONITOR_AND_PREPARE_SCALE_OUT";

    json result = {
        {"analyticsId", "SM_CONGESTION"},
        {"ts",          nowISO()},
        {"dnn",         config_.served_dnn},
        {"snssai",      {{"sst", config_.served_snssai_sst},
                         {"sd",  config_.served_snssai_sd}}},
        {"congestionLevel",   level},
        {"sessionEstAttempts", attempts},
        {"sessionEstFailures", failed},
        {"sessionEstFailureRatePct", std::round(failure_ratio * 10.0) / 10.0},
        {"sessionReleaseCount", released},
        {"cpLoadPct",   cp_load},
        {"upLoadPct",   upf_load},
        {"smcceUeList", {{"highLevelCongestion",   high_ues},
                         {"mediumLevelCongestion", med_ues},
                         {"lowLevelCongestion",    low_ues}}},
        {"recommendation", recommendation},
        {"dataPoints",  (int)smf.size()},
        {"confidence",  (int)computeConfidence((int)smf.size(), 1, 100,
                                               metrics.empty() ? 0.7 : 1.0)}
    };
    // The slice/DNN reported above is the NWDAF's single served slice from
    // config; per-slice decomposition arrives with S-NSSAI threading (H1.2).
    result["note"] = "Single-slice scope: analytics reflect the configured "
                     "served S-NSSAI/DNN; per-slice decomposition requires "
                     "S-NSSAI threading (H1.2).";
    if (!supi.empty()) result["supi"] = supi;
    return result;
}

// ── REDUNDANT_TRANSMISSION (TS 23.288 §6.12 — RED_TRANS_EXP) ─────────────────

json NwdafAnalyticsEngine::redundantTransmission(const std::string& supi,
                                                  const std::string& start_ts,
                                                  const std::string& end_ts) {
    auto hist = filterByWindow(collector_.getThroughputHistory(360), start_ts, end_ts);
    int n = (int)hist.size();

    if (n < 2) {
        json r = {
            {"analyticsId", "REDUNDANT_TRANSMISSION"},
            {"ts",          nowISO()},
            {"reason",      "INSUFFICIENT_DATA"},
            {"dataPoints",  n},
            {"confidence",  0}
        };
        if (!supi.empty()) r["supi"] = supi;
        return r;
    }

    std::vector<double> dl, ul;
    dl.reserve(n); ul.reserve(n);
    for (const auto& s : hist) {
        dl.push_back(s.total_dl_kbps);
        ul.push_back(s.total_ul_kbps);
    }

    auto mean_of = [](const std::vector<double>& v) {
        return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    };
    auto var_of = [&](const std::vector<double>& v) {
        double m = mean_of(v), acc = 0.0;
        for (double x : v) acc += (x - m) * (x - m);
        return acc / v.size();
    };

    // Redundant-transmission experience is a delivery-success percentage in the
    // spec. Without dual-path GTP-U counters we proxy it with per-direction
    // rate stability: a steady rate implies packets are landing on schedule,
    // a volatile one implies retransmission/reordering pressure.
    auto experience = [&](const std::vector<double>& v) {
        double cov = coefficientOfVariation(v);
        return std::clamp(100.0 * (1.0 - std::min(1.0, cov)), 0.0, 100.0);
    };

    double dl_exp = experience(dl), ul_exp = experience(ul);
    double dl_var = var_of(dl),     ul_var = var_of(ul);

    // Per-time-slot experience (§6.12 redTransExpPerTS) — the window split into
    // equal slots so a consumer can see when reliability dipped.
    const int slots = std::min(6, n);
    json per_ts = json::array();
    for (int i = 0; i < slots; ++i) {
        int lo = (int)((long)i * n / slots);
        int hi = (int)((long)(i + 1) * n / slots);
        if (hi <= lo) continue;
        std::vector<double> d_slot(dl.begin() + lo, dl.begin() + hi);
        std::vector<double> u_slot(ul.begin() + lo, ul.begin() + hi);
        per_ts.push_back({
            {"slot",           i},
            {"startTs",        hist[lo].timestamp_iso},
            {"endTs",          hist[hi - 1].timestamp_iso},
            {"dlRedTransExp",  std::round(experience(d_slot) * 10.0) / 10.0},
            {"ulRedTransExp",  std::round(experience(u_slot) * 10.0) / 10.0}
        });
    }

    double worst = std::min(dl_exp, ul_exp);
    std::string reliability = worst >= 95.0 ? "HIGH"
                            : worst >= 80.0 ? "MEDIUM" : "LOW";
    // URLLC needs a consistently high delivery ratio; anything volatile fails it.
    bool urllc_suitable = worst >= 95.0;

    json result = {
        {"analyticsId", "REDUNDANT_TRANSMISSION"},
        {"ts",          nowISO()},
        {"snssai",      {{"sst", config_.served_snssai_sst},
                         {"sd",  config_.served_snssai_sd}}},
        {"redTransExp", {
            {"avgDlRedTransExp", std::round(dl_exp * 10.0) / 10.0},
            {"varDlRedTransExp", std::round(dl_var * 100.0) / 100.0},
            {"avgUlRedTransExp", std::round(ul_exp * 10.0) / 10.0},
            {"varUlRedTransExp", std::round(ul_var * 100.0) / 100.0}
        }},
        {"redTransExpPerTS",  per_ts},
        {"reliabilityLevel",  reliability},
        {"urllcSuitable",     urllc_suitable},
        {"avgDlKbps",         std::round(mean_of(dl) * 10.0) / 10.0},
        {"avgUlKbps",         std::round(mean_of(ul) * 10.0) / 10.0},
        {"dataPoints",        n},
        {"confidence",        (int)computeConfidence(n, 2, 360)},
        {"note", "Experience is derived from per-direction rate stability: "
                 "true per-path delivery ratios require dual-path GTP-U "
                 "(N3/N9) counters, which the /sys/class/net data path cannot "
                 "expose (gtp5g constraint). Superseded by PFCP usage "
                 "reporting (H1.3)."}
    };
    if (!supi.empty()) {
        result["supi"]         = supi;
        result["supiFiltered"] = false;
    }
    return result;
}

// ── DISPERSION (TS 23.288 §6.10) ─────────────────────────────────────────────

json NwdafAnalyticsEngine::dispersion(const std::string& supi,
                                       const std::string& start_ts,
                                       const std::string& end_ts) {
    // ── DVDA: data-volume dispersion across the observation window ───────────
    auto hist = filterByWindow(collector_.getThroughputHistory(360), start_ts, end_ts);
    std::vector<double> volumes;
    volumes.reserve(hist.size());
    for (const auto& s : hist)
        volumes.push_back(s.total_dl_kbps + s.total_ul_kbps);

    double vol_gini = giniCoefficient(volumes);
    double vol_cov  = coefficientOfVariation(volumes);

    // Share of total volume carried by the busiest decile of samples — the
    // concentration measure operators actually plan capacity against.
    double top_decile_share = 0.0;
    if (!volumes.empty()) {
        std::vector<double> sorted = volumes;
        std::sort(sorted.rbegin(), sorted.rend());
        size_t top_n = std::max<size_t>(1, sorted.size() / 10);
        double top_sum = std::accumulate(sorted.begin(), sorted.begin() + top_n, 0.0);
        double all_sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
        if (all_sum > 0.0) top_decile_share = 100.0 * top_sum / all_sum;
    }

    // ── TDA: transaction dispersion across subscribers ───────────────────────
    auto amf = filterAmfByWindow(collector_.getRecentAmfEvents(500), start_ts, end_ts);
    auto smf = filterSmfByWindow(collector_.getRecentSmfEvents(500), start_ts, end_ts);
    if (!supi.empty()) {
        amf = filterAmfBySupi(amf, supi);
        smf = filterSmfBySupi(smf, supi);
    }

    std::map<std::string,int> tx_per_ue;
    for (const auto& e : amf) if (!e.supi.empty()) ++tx_per_ue[e.supi];
    for (const auto& e : smf) if (!e.supi.empty()) ++tx_per_ue[e.supi];

    std::vector<double> tx_counts;
    tx_counts.reserve(tx_per_ue.size());
    for (const auto& [ue, c] : tx_per_ue) { (void)ue; tx_counts.push_back((double)c); }

    double tx_gini = giniCoefficient(tx_counts);
    double total_tx = std::accumulate(tx_counts.begin(), tx_counts.end(), 0.0);

    // Herfindahl-Hirschman index over subscriber transaction shares.
    double hhi = 0.0;
    if (total_tx > 0.0)
        for (double c : tx_counts) { double sh = c / total_tx; hhi += sh * sh; }

    // Top talkers, ranked — the §6.10 "usage rank" idea applied to subscribers.
    std::vector<std::pair<std::string,int>> ranked(tx_per_ue.begin(), tx_per_ue.end());
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    json top_talkers = json::array();
    for (size_t i = 0; i < ranked.size() && i < 5; ++i) {
        double share = total_tx > 0.0 ? 100.0 * ranked[i].second / total_tx : 0.0;
        top_talkers.push_back({
            {"supi",            ranked[i].first},
            {"transactions",    ranked[i].second},
            {"sharePct",        std::round(share * 10.0) / 10.0},
            {"dispersionClass", bandOf(share, 20.0, 40.0)}
        });
    }

    json result = {
        {"analyticsId",   "DISPERSION"},
        {"ts",            nowISO()},
        {"snssai",        {{"sst", config_.served_snssai_sst},
                           {"sd",  config_.served_snssai_sd}}},
        {"dataVolumeDispersion", {
            {"giniCoefficient",        std::round(vol_gini * 1000.0) / 1000.0},
            {"coefficientOfVariation", std::round(vol_cov * 1000.0) / 1000.0},
            {"topDecileSharePct",      std::round(top_decile_share * 10.0) / 10.0},
            {"dispersionClass",        bandOf(vol_gini, 0.2, 0.4)},
            {"samples",                (int)volumes.size()}
        }},
        {"transactionDispersion", {
            {"ueCount",           (int)tx_counts.size()},
            {"totalTransactions", (int)total_tx},
            {"giniCoefficient",   std::round(tx_gini * 1000.0) / 1000.0},
            {"hhi",               std::round(hhi * 1000.0) / 1000.0},
            {"dispersionClass",   bandOf(tx_gini, 0.2, 0.4)},
            {"topTalkers",        top_talkers}
        }},
        {"dataPoints",  (int)volumes.size()},
        {"confidence",  (int)computeConfidence((int)volumes.size(), 1, 360)},
        {"note", "Dispersion is computed over the time and subscriber "
                 "dimensions. The §6.10 location dimension (per-TA/per-cell "
                 "ueDispersionType FIXED/CAMPER/TRAVELLER) needs cell-level "
                 "input from Namf_EventExposure, which the journald data path "
                 "does not carry (H1.1)."}
    };
    if (!supi.empty()) result["supi"] = supi;
    return result;
}
