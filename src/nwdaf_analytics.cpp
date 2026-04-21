#include "nwdaf_analytics.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <filesystem>

const std::set<std::string> NwdafAnalyticsEngine::VALID_ANALYTICS_IDS = {
    "NF_LOAD", "UE_MOBILITY", "UE_COMMUNICATION",
    "ABNORMAL_BEHAVIOUR", "QoS_SUSTAINABILITY",
    "SERVICE_EXPERIENCE", "NETWORK_PERFORMANCE"
};

NwdafAnalyticsEngine::NwdafAnalyticsEngine(NwdafCollector& collector,
                                           const NwdafConfig& config)
    : collector_(collector),
      config_(config),
      anomaly_model_(100, config.anomaly_contamination, 42)
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

void NwdafAnalyticsEngine::loadModels() {
    std::string path = config_.model_dir + "/isolation_forest.json";
    try {
        anomaly_model_.load(path);
        spdlog::info("Loaded anomaly model from {}", path);
    } catch (...) {
        spdlog::info("No pre-trained anomaly model found — operating in rule-based mode");
    }
}

void NwdafAnalyticsEngine::saveModels() {
    try {
        std::filesystem::create_directories(config_.model_dir);
        std::string path = config_.model_dir + "/isolation_forest.json";
        anomaly_model_.save(path);
        spdlog::info("Saved anomaly model to {}", path);
    } catch (const std::exception& e) {
        spdlog::warn("Failed to save anomaly model: {}", e.what());
    }
}

// BUG-04: explicit retrain — always calls fit(), regardless of isFitted() state
json NwdafAnalyticsEngine::retrain() {
    auto hist = collector_.getThroughputHistory(360);
    if ((int)hist.size() < config_.anomaly_min_samples) {
        return {
            {"status",     "skipped"},
            {"reason",     "INSUFFICIENT_DATA"},
            {"dataPoints", (int)hist.size()}
        };
    }

    std::vector<std::array<double,2>> X;
    X.reserve(hist.size());
    for (const auto& s : hist) X.push_back({s.total_dl_kbps, s.total_ul_kbps});

    {
        std::unique_lock<std::shared_mutex> lk(ml_mutex_);
        anomaly_model_.fit(X);  // always fits, even if already fitted
    }
    saveModels();

    return {
        {"status",     "trained"},
        {"dataPoints", (int)X.size()},
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

json NwdafAnalyticsEngine::nfLoad(const std::string&, const std::string&, const std::string&) {
    auto metrics = collector_.getCachedNfMetrics();
    if (metrics.empty()) metrics = collector_.collectNfLoad();

    json load_list = json::array();
    std::vector<std::string> overloaded;

    for (const auto& m : metrics) {
        json entry = {
            {"nfType",   m.nf_type},
            {"nfStatus", m.status},
            {"nfLoadLevelInfo", {
                {"nfLoadLevel",      m.load_pct},
                {"nfLoadLevelLabel", m.load_label},
                {"nfCpuUsage",       m.cpu_seconds},
                {"nfMemoryUsage",    m.mem_kb}
            }}
        };
        load_list.push_back(entry);
        if (m.load_label == "OVERLOADED") overloaded.push_back(m.nf_type);
    }

    std::string recommendation = overloaded.empty() ? "STABLE" : "SCALE_OUT";
    int confidence = overloaded.empty() ? 90 : 75;

    return {
        {"analyticsId",    "NF_LOAD"},
        {"ts",             nowISO()},
        {"nfLoadLevelList", load_list},
        {"overloadedNfs",  overloaded},
        {"recommendation", recommendation},
        {"confidence",     confidence}
    };
}

// ── UE_MOBILITY ───────────────────────────────────────────────────────────────

json NwdafAnalyticsEngine::ueMobility(const std::string& supi, const std::string&, const std::string&) {
    auto events = collector_.getRecentAmfEvents(500);

    int reg = 0, dereg = 0, handover = 0, auth_fail = 0;
    for (const auto& e : events) {
        if (!supi.empty() && e.supi != supi) continue;
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

json NwdafAnalyticsEngine::ueCommunication(const std::string&, const std::string&, const std::string&) {
    auto smf = collector_.getRecentSmfEvents(500);
    int established = 0, released = 0;
    for (const auto& e : smf) {
        if      (e.event_type == "PDU_ESTABLISHED") ++established;
        else if (e.event_type == "PDU_RELEASED")    ++released;
    }
    int active = std::max(0, established - released);

    auto hist = collector_.getThroughputHistory(1);
    double dl_kbps = hist.empty() ? 0.0 : hist.back().total_dl_kbps;
    double ul_kbps = hist.empty() ? 0.0 : hist.back().total_ul_kbps;

    int subscribers = collector_.getSubscriberCount();

    return {
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
}

// ── ABNORMAL_BEHAVIOUR ────────────────────────────────────────────────────────

json NwdafAnalyticsEngine::abnormalBehaviour(const std::string&, const std::string&, const std::string&) {
    auto hist = collector_.getThroughputHistory(360);
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

    // Build feature matrix
    std::vector<std::array<double,2>> X;
    X.reserve(n);
    for (const auto& s : hist) X.push_back({s.total_dl_kbps, s.total_ul_kbps});

    // BUG-03: exclusive lock for fit; shared lock for predict/score
    bool just_fitted = false;
    {
        std::unique_lock<std::shared_mutex> lk(ml_mutex_);
        if (!anomaly_model_.isFitted()) {
            anomaly_model_.fit(X);
            just_fitted = true;
        }
    }
    if (just_fitted) saveModels();  // I/O outside lock; only happens once (first call)

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
    if      (dl_std > 50)         anomaly_type = "UNEXPECTED_LARGE_RATE";
    else if (anomaly_pct > 20)    anomaly_type = "UNEXPECTED_WAKEUP";

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
        {"confidence",     92}
    };
}

// ── QoS_SUSTAINABILITY ────────────────────────────────────────────────────────

json NwdafAnalyticsEngine::qosSustainability(const std::string&, const std::string&, const std::string&) {
    // BUG-02: EWMA state is updated solely in NwdafCollector::bgLoop.
    // Read predictions here without mutating shared state — makes this call idempotent.
    auto hist = collector_.getThroughputHistory(1);
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

    return {
        {"analyticsId",   "QoS_SUSTAINABILITY"},
        {"ts",            nowISO()},
        {"currentDlKbps", cur_dl},
        {"currentUlKbps", cur_ul},
        {"predictedDlKbps", pred_dl},
        {"predictedUlKbps", pred_ul},
        {"dlTrend",        dl_trend},
        {"ulTrend",        ul_trend},
        {"violationRisk",  violation_risk},
        {"confidence",     80}
    };
}

// ── SERVICE_EXPERIENCE ────────────────────────────────────────────────────────

json NwdafAnalyticsEngine::serviceExperience(const std::string&, const std::string&, const std::string&) {
    auto hist = collector_.getThroughputHistory(1);
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

    // active session ratio
    auto smf = collector_.getRecentSmfEvents(500);
    int est = 0, rel = 0;
    for (const auto& e : smf) {
        if      (e.event_type == "PDU_ESTABLISHED") ++est;
        else if (e.event_type == "PDU_RELEASED")    ++rel;
    }
    int active = std::max(0, est - rel);
    int subscribers = collector_.getSubscriberCount();
    double ratio = (subscribers > 0) ? (double)active / subscribers : 0.0;

    return {
        {"analyticsId",      "SERVICE_EXPERIENCE"},
        {"ts",               nowISO()},
        {"mosScore",         mos},
        {"mosCategory",      category},
        {"dlKbps",           dl_kbps},
        {"ulKbps",           ul_kbps},
        {"activeSessionRatio", ratio},
        {"confidence",       75}
    };
}

// ── NETWORK_PERFORMANCE ───────────────────────────────────────────────────────

json NwdafAnalyticsEngine::networkPerformance(const std::string&, const std::string&, const std::string&) {
    auto metrics = collector_.getCachedNfMetrics();
    if (metrics.empty()) metrics = collector_.collectNfLoad();

    int total_nf = (int)metrics.size();
    int active_nf = 0;
    for (const auto& m : metrics)
        if (m.status == "active") ++active_nf;

    double nf_health = total_nf > 0 ? 100.0 * active_nf / total_nf : 0.0;

    auto hist = collector_.getThroughputHistory(1);
    double dl_kbps = hist.empty() ? 0.0 : hist.back().total_dl_kbps;

    auto smf = collector_.getRecentSmfEvents(500);
    int est = 0, rel = 0;
    for (const auto& e : smf) {
        if      (e.event_type == "PDU_ESTABLISHED") ++est;
        else if (e.event_type == "PDU_RELEASED")    ++rel;
    }
    int active_sessions = std::max(0, est - rel);

    double dl_score  = std::min(100.0, dl_kbps / 10.0);
    double pdu_score = std::min(100.0, (double)active_sessions * 20.0);

    double overall = 0.6 * nf_health + 0.2 * dl_score + 0.2 * pdu_score;

    std::string label = "POOR";
    if      (overall > 90) label = "EXCELLENT";
    else if (overall > 75) label = "GOOD";
    else if (overall > 50) label = "FAIR";

    return {
        {"analyticsId", "NETWORK_PERFORMANCE"},
        {"ts",          nowISO()},
        {"overallScore", overall},
        {"scoreLabel",   label},
        {"components", {
            {"nfHealthScore", nf_health},
            {"dlScore",       dl_score},
            {"pduScore",      pdu_score}
        }},
        {"confidence", 82}
    };
}
