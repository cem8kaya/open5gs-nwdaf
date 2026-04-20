#pragma once
#include "nwdaf_config.hpp"
#include "nwdaf_collector.hpp"
#include "ml/isolation_forest.hpp"
#include "ml/ewma_predictor.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <set>

using json = nlohmann::json;

class NwdafAnalyticsEngine {
public:
    explicit NwdafAnalyticsEngine(NwdafCollector& collector,
                                  const NwdafConfig& config);

    json compute(const std::string& analytics_id,
                 const std::string& supi = "",
                 const std::string& start_ts = "",
                 const std::string& end_ts = "");

    static const std::set<std::string> VALID_ANALYTICS_IDS;

private:
    NwdafCollector& collector_;
    NwdafConfig     config_;
    IsolationForest anomaly_model_;
    EwmaPredictor   dl_ewma_;
    EwmaPredictor   ul_ewma_;

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
