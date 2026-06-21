#pragma once
#include "nwdaf_config.hpp"
#include "nwdaf_collector.hpp"
#include "ml/isolation_forest.hpp"
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <string>
#include <set>
#include <chrono>
#include <vector>
#include <array>

using json = nlohmann::json;

// ARCH-02: 5-dimensional feature vector for network anomaly detection.
// Encodes both throughput and system-state signals so the IsolationForest can
// detect multi-variate anomalies beyond simple DL/UL throughput spikes.
struct AnomalyFeatures {
    double dl_kbps;           // downlink throughput
    double ul_kbps;           // uplink throughput
    double nf_load_avg;       // average NF CPU load across all NFs
    double active_sessions;   // PDU session count (from stateful tracker)
    double auth_failure_rate; // auth failures per minute from AMF events

    std::array<double,5> encode() const {
        return {dl_kbps, ul_kbps, nf_load_avg, active_sessions, auth_failure_rate};
    }
};

class NwdafAnalyticsEngine {
public:
    explicit NwdafAnalyticsEngine(NwdafCollector& collector,
                                  const NwdafConfig& config);

    json compute(const std::string& analytics_id,
                 const std::string& supi = "",
                 const std::string& start_ts = "",
                 const std::string& end_ts = "");

    // BUG-04: explicit retrain — always fits, regardless of isFitted() state
    json retrain();

    // P2-5: Readiness check
    bool isReady() const;

    // PROD-03: expose collector data for Prometheus metrics endpoint
    std::pair<double,double>   getCurrentThroughput() const;   // {dl_kbps, ul_kbps}
    std::vector<NfMetric>      getCurrentNfMetrics()  const;

    static const std::set<std::string> VALID_ANALYTICS_IDS;

    // ARCH-01: data-quality-driven confidence computation (public for testability)
    // Returns 0 if data_points < min_points; otherwise scales linearly with
    // coverage (data_points / max_points), reduced by baseline_quality, capped at 95.
    static double computeConfidence(int data_points, int min_points,
                                    int max_points, double baseline_quality = 1.0);

    // COMP-04: time window helpers (public for testability)
    static std::chrono::system_clock::time_point parseISO(const std::string& ts);
    static bool inWindow(const std::string& event_ts,
                         const std::string& start_ts,
                         const std::string& end_ts);
    static std::vector<ThroughputSample> filterByWindow(
        const std::vector<ThroughputSample>& hist,
        const std::string& start_ts,
        const std::string& end_ts);
    static std::vector<AmfEvent> filterAmfByWindow(
        const std::vector<AmfEvent>& events,
        const std::string& start_ts,
        const std::string& end_ts);
    static std::vector<SmfEvent> filterSmfByWindow(
        const std::vector<SmfEvent>& events,
        const std::string& start_ts,
        const std::string& end_ts);

    // COMP-03: SUPI filter helpers (public for testability)
    static std::vector<AmfEvent> filterAmfBySupi(
        const std::vector<AmfEvent>& events, const std::string& supi);
    static std::vector<SmfEvent> filterSmfBySupi(
        const std::vector<SmfEvent>& events, const std::string& supi);

    // PROD-04: hot-reload analytics config from SIGHUP handler
    void updateConfig(double anomaly_contamination);

private:
    NwdafCollector& collector_;
    NwdafConfig     config_;
    // ARCH-02: 5-feature anomaly model replaces the previous 2-feature model.
    // The IsolationForest alias still works for tests that use the 2-D variant.
    IsolationForestN<5> anomaly_model_;
    // BUG-03: protects anomaly_model_ for concurrent /train + GET /analytics
    mutable std::shared_mutex ml_mutex_;
    // BUG-02: EWMA predictors removed — now live in NwdafCollector::bgLoop

    json nfLoad(const std::string& supi, const std::string& start_ts, const std::string& end_ts);
    json ueMobility(const std::string& supi, const std::string& start_ts, const std::string& end_ts);
    json ueCommunication(const std::string& supi, const std::string& start_ts, const std::string& end_ts);
    json abnormalBehaviour(const std::string& supi, const std::string& start_ts, const std::string& end_ts);
    json qosSustainability(const std::string& supi, const std::string& start_ts, const std::string& end_ts);
    json serviceExperience(const std::string& supi, const std::string& start_ts, const std::string& end_ts);
    json networkPerformance(const std::string& supi, const std::string& start_ts, const std::string& end_ts);

    void loadModels();
    void saveModels();
    std::string nowISO() const;
};
