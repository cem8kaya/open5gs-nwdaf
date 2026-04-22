#pragma once
#include "nwdaf_analytics.hpp"
#include "nwdaf_subscription.hpp"
#include "nwdaf_config.hpp"
#include "nwdaf_ratelimit.hpp"
#include <httplib.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>

// PROD-03: analytics values updated atomically after each compute() call.
// Protected by NwdafServer::metrics_mutex_.
struct NwdafMetricsCache {
    double anomaly_pct               = 0.0;
    double mos_score                 = 2.0;
    double network_performance_score = 0.0;
};

class NwdafServer {
public:
    NwdafServer(NwdafAnalyticsEngine& engine,
                NwdafSubscriptionStore& subs,
                const NwdafConfig& config);

    void start();
    void stop();

    // PROD-03: notification counters incremented by NwdafNotifier
    std::atomic<uint64_t> notif_total_{0};
    std::atomic<uint64_t> notif_failures_{0};

private:
    // ARCH-05: unique_ptr allows substituting SSLServer at runtime when
    // tls_enabled=true without changing the rest of the server code.
    std::unique_ptr<httplib::Server> svr_;

    void setupRoutes();

    void handleHealth(const httplib::Request&, httplib::Response&);
    void handleGetAnalytics(const httplib::Request&, httplib::Response&);
    void handleCreateSubscription(const httplib::Request&, httplib::Response&);
    void handleGetSubscription(const httplib::Request&, httplib::Response&);
    void handleDeleteSubscription(const httplib::Request&, httplib::Response&);
    void handleListSubscriptions(const httplib::Request&, httplib::Response&);
    void handleTrainModel(const httplib::Request&, httplib::Response&);
    void handleMetrics(const httplib::Request&, httplib::Response&);  // PROD-03

    NwdafAnalyticsEngine&   engine_;
    NwdafSubscriptionStore& subs_;
    NwdafConfig             config_;

    // PROD-06: per-IP + global token-bucket rate limiter
    RateLimiter rate_limiter_;

    // PROD-03: per-analytics-ID request counters + last-computed value cache
    mutable std::mutex              metrics_mutex_;
    std::map<std::string, uint64_t> analytics_request_counts_;
    NwdafMetricsCache               metrics_cache_;
};
