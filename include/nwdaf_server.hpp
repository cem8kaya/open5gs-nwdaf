#pragma once
#include "nwdaf_analytics.hpp"
#include "nwdaf_subscription.hpp"
#include "nwdaf_config.hpp"
#include <httplib.h>

class NwdafServer {
public:
    NwdafServer(NwdafAnalyticsEngine& engine,
                NwdafSubscriptionStore& subs,
                const NwdafConfig& config);

    void start();
    void stop();

private:
    httplib::Server svr_;

    void setupRoutes();

    void handleHealth(const httplib::Request&, httplib::Response&);
    void handleGetAnalytics(const httplib::Request&, httplib::Response&);
    void handleCreateSubscription(const httplib::Request&, httplib::Response&);
    void handleGetSubscription(const httplib::Request&, httplib::Response&);
    void handleDeleteSubscription(const httplib::Request&, httplib::Response&);
    void handleListSubscriptions(const httplib::Request&, httplib::Response&);
    void handleTrainModel(const httplib::Request&, httplib::Response&);

    NwdafAnalyticsEngine&   engine_;
    NwdafSubscriptionStore& subs_;
    NwdafConfig             config_;
};
