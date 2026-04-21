#pragma once
#include "nwdaf_subscription.hpp"
#include "nwdaf_analytics.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <string>

// NwdafNotifier — background push-delivery thread (TS 29.520 §5.3.3).
//
// Every poll_interval_seconds the delivery loop checks each ACTIVE subscription.
// If now - last_delivered[sub_id] >= rep_period_seconds it calls engine_.compute()
// and POSTs the result to sub.notif_uri.  On HTTP 2xx the report_count is
// incremented; when max_report_nbr is reached the subscription is auto-deleted.
// Delivery failures are logged as warnings and retried on the next interval.
class NwdafNotifier {
public:
    NwdafNotifier(NwdafSubscriptionStore& subs,
                  NwdafAnalyticsEngine&   engine,
                  int poll_interval_seconds = 5);

    void start();
    void stop();

private:
    void deliveryLoop();
    void deliver(const Subscription& sub);

    NwdafSubscriptionStore& subs_;
    NwdafAnalyticsEngine&   engine_;
    int                     poll_interval_s_;
    std::thread             thread_;
    std::atomic<bool>       running_{false};

    mutable std::mutex ts_mutex_;
    std::unordered_map<std::string,
                       std::chrono::steady_clock::time_point> last_delivered_;
};
