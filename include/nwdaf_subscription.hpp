#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct Subscription {
    std::string sub_id;
    std::string analytics_id;
    std::string notif_uri;
    std::string notif_id;
    int         rep_period_seconds;
    int         max_report_nbr;
    int         report_count = 0;   // incremented by NwdafNotifier on each successful delivery
    std::string created_at_iso;
    std::string status;
};

class NwdafSubscriptionStore {
public:
    std::string               create(const json& body);
    bool                      exists(const std::string& sub_id) const;
    Subscription              get(const std::string& sub_id) const;
    bool                      remove(const std::string& sub_id);
    std::vector<Subscription> listAll() const;
    int                       count() const;
    // Atomically increment report_count; returns new count, or -1 if not found
    int                       incrementReportCount(const std::string& sub_id);

private:
    mutable std::mutex                            mutex_;
    std::unordered_map<std::string, Subscription> store_;
};
