#include "nwdaf_server.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

NwdafServer::NwdafServer(NwdafAnalyticsEngine& engine,
                         NwdafSubscriptionStore& subs,
                         const NwdafConfig& config)
    : engine_(engine), subs_(subs), config_(config)
{
    setupRoutes();
}

static json errorResponse(int status, const std::string& title, const std::string& cause) {
    return {{"title", title}, {"status", status}, {"cause", cause}};
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

void NwdafServer::setupRoutes() {
    svr_.Get("/nwdaf-analytics/v1/health",
        [this](const httplib::Request& req, httplib::Response& res) {
            handleHealth(req, res);
        });

    svr_.Get("/nwdaf-analytics/v1/analytics",
        [this](const httplib::Request& req, httplib::Response& res) {
            handleGetAnalytics(req, res);
        });

    svr_.Post("/nwdaf-analytics/v1/subscriptions",
        [this](const httplib::Request& req, httplib::Response& res) {
            handleCreateSubscription(req, res);
        });

    svr_.Get("/nwdaf-analytics/v1/subscriptions",
        [this](const httplib::Request& req, httplib::Response& res) {
            handleListSubscriptions(req, res);
        });

    svr_.Get(R"(/nwdaf-analytics/v1/subscriptions/([^/]+))",
        [this](const httplib::Request& req, httplib::Response& res) {
            handleGetSubscription(req, res);
        });

    svr_.Delete(R"(/nwdaf-analytics/v1/subscriptions/([^/]+))",
        [this](const httplib::Request& req, httplib::Response& res) {
            handleDeleteSubscription(req, res);
        });

    svr_.Post("/nwdaf-analytics/v1/train",
        [this](const httplib::Request& req, httplib::Response& res) {
            handleTrainModel(req, res);
        });
}

void NwdafServer::start() {
    spdlog::info("NWDAF SBI server starting on {}:{}", config_.sbi_bind_address, config_.sbi_port);
    svr_.listen(config_.sbi_bind_address.c_str(), config_.sbi_port);
}

void NwdafServer::stop() {
    svr_.stop();
}

void NwdafServer::handleHealth(const httplib::Request&, httplib::Response& res) {
    json body = {{"status", "UP"}, {"nfType", "NWDAF"},
                 {"nfInstanceId", config_.nf_instance_id},
                 {"ts", nowISO()}};
    res.set_content(body.dump(), "application/json");
    res.status = 200;
}

void NwdafServer::handleGetAnalytics(const httplib::Request& req, httplib::Response& res) {
    if (!req.has_param("analyticsId")) {
        res.set_content(errorResponse(400, "Missing parameter", "analyticsId is required").dump(),
                        "application/json");
        res.status = 400;
        return;
    }

    std::string analytics_id = req.get_param_value("analyticsId");
    if (NwdafAnalyticsEngine::VALID_ANALYTICS_IDS.find(analytics_id) ==
        NwdafAnalyticsEngine::VALID_ANALYTICS_IDS.end())
    {
        res.set_content(errorResponse(422, "Unprocessable Entity",
                                      "Unknown analyticsId: " + analytics_id).dump(),
                        "application/json");
        res.status = 422;
        return;
    }

    std::string supi     = req.has_param("supi")    ? req.get_param_value("supi")    : "";
    std::string start_ts = req.has_param("startTs") ? req.get_param_value("startTs") : "";
    std::string end_ts   = req.has_param("endTs")   ? req.get_param_value("endTs")   : "";

    try {
        std::string req_time = nowISO();
        json anal_data = engine_.compute(analytics_id, supi, start_ts, end_ts);
        int confidence = anal_data.value("confidence", 80);

        json response = {
            {"analyticsId",  analytics_id},
            {"requestTime",  req_time},
            {"timeStampGen", nowISO()},
            {"validity",     60},
            {"confidence",   confidence},
            {"analData",     anal_data}
        };
        res.set_content(response.dump(), "application/json");
        res.status = 200;
    } catch (const std::exception& e) {
        res.set_content(errorResponse(500, "Internal Error", e.what()).dump(),
                        "application/json");
        res.status = 500;
    }
}

void NwdafServer::handleCreateSubscription(const httplib::Request& req, httplib::Response& res) {
    try {
        json body = json::parse(req.body);
        if (!body.contains("analyticsId") || !body.contains("notifUri")) {
            res.set_content(errorResponse(400, "Bad Request",
                                          "analyticsId and notifUri are required").dump(),
                            "application/json");
            res.status = 400;
            return;
        }
        std::string sub_id = subs_.create(body);
        auto sub = subs_.get(sub_id);
        json response = {
            {"subId",       sub_id},
            {"analyticsId", sub.analytics_id},
            {"notifUri",    sub.notif_uri},
            {"status",      sub.status},
            {"createdAt",   sub.created_at_iso}
        };
        res.set_content(response.dump(), "application/json");
        res.status = 201;
    } catch (const std::exception& e) {
        res.set_content(errorResponse(400, "Bad Request", e.what()).dump(), "application/json");
        res.status = 400;
    }
}

void NwdafServer::handleGetSubscription(const httplib::Request& req, httplib::Response& res) {
    std::string sub_id = req.matches[1];
    if (!subs_.exists(sub_id)) {
        res.set_content(errorResponse(404, "Not Found", "Subscription not found").dump(),
                        "application/json");
        res.status = 404;
        return;
    }
    auto sub = subs_.get(sub_id);
    json response = {
        {"subId",       sub.sub_id},
        {"analyticsId", sub.analytics_id},
        {"notifUri",    sub.notif_uri},
        {"status",      sub.status},
        {"createdAt",   sub.created_at_iso}
    };
    res.set_content(response.dump(), "application/json");
    res.status = 200;
}

void NwdafServer::handleDeleteSubscription(const httplib::Request& req, httplib::Response& res) {
    std::string sub_id = req.matches[1];
    if (!subs_.remove(sub_id)) {
        res.set_content(errorResponse(404, "Not Found", "Subscription not found").dump(),
                        "application/json");
        res.status = 404;
        return;
    }
    res.status = 204;
}

void NwdafServer::handleListSubscriptions(const httplib::Request&, httplib::Response& res) {
    auto all = subs_.listAll();
    json arr = json::array();
    for (const auto& sub : all) {
        arr.push_back({
            {"subId",       sub.sub_id},
            {"analyticsId", sub.analytics_id},
            {"notifUri",    sub.notif_uri},
            {"status",      sub.status},
            {"createdAt",   sub.created_at_iso}
        });
    }
    res.set_content(arr.dump(), "application/json");
    res.status = 200;
}

void NwdafServer::handleTrainModel(const httplib::Request&, httplib::Response& res) {
    // BUG-04: use explicit retrain() which always fits and returns training status,
    // rather than compute("ABNORMAL_BEHAVIOUR") which only fits once and wastes compute.
    try {
        json result = engine_.retrain();
        res.set_content(result.dump(), "application/json");
        res.status = 200;
    } catch (const std::exception& e) {
        res.set_content(errorResponse(500, "Training failed", e.what()).dump(), "application/json");
        res.status = 500;
    }
}
