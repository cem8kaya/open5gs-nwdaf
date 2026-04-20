#include "nwdaf_subscription.hpp"
#include <stdexcept>
#include <random>
#include <sstream>
#include <iomanip>

static std::string generateSubId() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    std::ostringstream oss;
    oss << "sub-" << std::hex << std::setw(16) << std::setfill('0') << dis(gen);
    return oss.str();
}

static std::string nowISO() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    gmtime_r(&t, &tm_buf);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return buf;
}

std::string NwdafSubscriptionStore::create(const json& body) {
    Subscription sub;
    sub.sub_id        = generateSubId();
    sub.analytics_id  = body.value("analyticsId", "");
    sub.notif_uri     = body.value("notifUri", "");
    sub.notif_id      = body.value("notifId", "");
    sub.rep_period_seconds = body.value("repPeriod", 60);
    sub.max_report_nbr     = body.value("maxReportNbr", 0);
    sub.created_at_iso     = nowISO();
    sub.status             = "ACTIVE";

    std::lock_guard<std::mutex> lk(mutex_);
    store_[sub.sub_id] = sub;
    return sub.sub_id;
}

bool NwdafSubscriptionStore::exists(const std::string& sub_id) const {
    std::lock_guard<std::mutex> lk(mutex_);
    return store_.count(sub_id) > 0;
}

Subscription NwdafSubscriptionStore::get(const std::string& sub_id) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = store_.find(sub_id);
    if (it == store_.end()) throw std::out_of_range("Subscription not found: " + sub_id);
    return it->second;
}

bool NwdafSubscriptionStore::remove(const std::string& sub_id) {
    std::lock_guard<std::mutex> lk(mutex_);
    return store_.erase(sub_id) > 0;
}

std::vector<Subscription> NwdafSubscriptionStore::listAll() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<Subscription> out;
    out.reserve(store_.size());
    for (const auto& [k, v] : store_) out.push_back(v);
    return out;
}

int NwdafSubscriptionStore::count() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return (int)store_.size();
}
