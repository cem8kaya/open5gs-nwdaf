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

using json = nlohmann::json;

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

    static const std::set<std::string> VALID_ANALYTICS_IDS;

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

private:
    NwdafCollector& collector_;
    NwdafConfig     config_;
    IsolationForest anomaly_model_;
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
