#include "nwdaf_server.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <cstdlib>

NwdafServer::NwdafServer(NwdafAnalyticsEngine& engine,
                         NwdafSubscriptionStore& subs,
                         const NwdafConfig& config)
    : engine_(engine), subs_(subs), config_(config),
      rate_limiter_(config.rate_limit_per_ip_rps, config.rate_limit_global_rps)
{
    // ARCH-05: instantiate SSLServer when TLS is enabled and compiled in,
    // otherwise fall back to plain HTTP.
#ifdef NWDAF_USE_TLS
    if (config.tls_enabled) {
        auto ssl_svr = std::make_unique<httplib::SSLServer>(
            config.tls_cert_file.c_str(), config.tls_key_file.c_str());
        if (!ssl_svr->is_valid()) {
            spdlog::error("Failed to load TLS cert={} or key={}", config.tls_cert_file, config.tls_key_file);
            throw std::runtime_error("Invalid TLS certificate or key path");
        }
        svr_ = std::move(ssl_svr);
        spdlog::info("TLS enabled: cert={} key={}", config.tls_cert_file, config.tls_key_file);
    } else {
        svr_ = std::make_unique<httplib::Server>();
    }
#else
    if (config.tls_enabled) {
        spdlog::warn("tls_enabled=true but NWDAF_USE_TLS not compiled in — "
                     "falling back to plain HTTP (rebuild with -DNWDAF_USE_TLS=ON)");
    }
    svr_ = std::make_unique<httplib::Server>();
#endif
    setupRoutes();
}

static json errorResponse(int status, const std::string& title, const std::string& cause) {
    return {
        {"type", "about:blank"},
        {"title", title},
        {"status", status},
        {"detail", cause},
        {"cause", cause}
    };
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

// PROD-05: generate an 8-hex-char short request ID when the caller sends none
static std::string generateShortId() {
    static std::mt19937 rng(std::random_device{}());
    static std::mutex rng_mutex;
    std::lock_guard<std::mutex> lk(rng_mutex);
    std::uniform_int_distribution<uint32_t> dis;
    char buf[9];
    snprintf(buf, sizeof(buf), "%08x", dis(rng));
    return buf;
}

void NwdafServer::setupRoutes() {
    // P1-4: OAuth 2.0 access-token auth
    svr_->set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
        if (config_.oauth_enabled) {
            // Allow healthcheck without auth
            if (req.path == "/nwdaf-analytics/v1/health") {
                return httplib::Server::HandlerResponse::Unhandled;
            }
            if (req.has_header("Authorization")) {
                std::string auth = req.get_header_value("Authorization");
                if (auth.find("Bearer ") == 0) {
                    std::string token = auth.substr(7);
                    // Minimal token check: ensure it's not empty. 
                    // In a full implementation, verify signature or call NRF token endpoint.
                    if (!token.empty()) {
                        return httplib::Server::HandlerResponse::Unhandled;
                    }
                }
            }
            res.status = 401;
            res.set_content(errorResponse(401, "Unauthorized", "Missing or invalid OAuth2.0 access token").dump(),
                            "application/problem+json");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    svr_->Get("/nwdaf-analytics/v1/health",
        [this](const httplib::Request& req, httplib::Response& res) {
            handleHealth(req, res);
        });

    svr_->Get("/nwdaf-analytics/v1/analytics",
        [this](const httplib::Request& req, httplib::Response& res) {
            handleGetAnalytics(req, res);
        });

    // P1-1: Spec-compliant POST for Nnwdaf_AnalyticsInfo
    svr_->Post("/nnwdaf-analyticsinfo/v1/analytics",
        [this](const httplib::Request& req, httplib::Response& res) {
            handlePostAnalytics(req, res);
        });

    svr_->Post("/nwdaf-analytics/v1/subscriptions",
        [this](const httplib::Request& req, httplib::Response& res) {
            handleCreateSubscription(req, res);
        });

    svr_->Get("/nwdaf-analytics/v1/subscriptions",
        [this](const httplib::Request& req, httplib::Response& res) {
            handleListSubscriptions(req, res);
        });

    svr_->Get(R"(/nwdaf-analytics/v1/subscriptions/([^/]+))",
        [this](const httplib::Request& req, httplib::Response& res) {
            handleGetSubscription(req, res);
        });

    svr_->Delete(R"(/nwdaf-analytics/v1/subscriptions/([^/]+))",
        [this](const httplib::Request& req, httplib::Response& res) {
            handleDeleteSubscription(req, res);
        });

    svr_->Post("/nwdaf-analytics/v1/train",
        [this](const httplib::Request& req, httplib::Response& res) {
            handleTrainModel(req, res);
        });

    // PROD-03: Prometheus-format metrics scrape endpoint
    svr_->Get("/nwdaf-analytics/v1/metrics",
        [this](const httplib::Request& req, httplib::Response& res) {
            handleMetrics(req, res);
        });

    // P2-5: Readiness endpoint
    svr_->Get("/nwdaf-analytics/v1/ready",
        [this](const httplib::Request& req, httplib::Response& res) {
            handleReady(req, res);
        });

    // Traffic simulator endpoints
    svr_->Post("/nwdaf-analytics/v1/traffic/start",
        [this](const httplib::Request& req, httplib::Response& res) {
            handleTrafficStart(req, res);
        });

    svr_->Post("/nwdaf-analytics/v1/traffic/stop",
        [this](const httplib::Request& req, httplib::Response& res) {
            handleTrafficStop(req, res);
        });

    svr_->Get("/nwdaf-analytics/v1/traffic/status",
        [this](const httplib::Request& req, httplib::Response& res) {
            handleTrafficStatus(req, res);
        });
}

void NwdafServer::start() {
    spdlog::info("NWDAF SBI server starting on {}:{}", config_.sbi_bind_address, config_.sbi_port);
    svr_->listen(config_.sbi_bind_address.c_str(), config_.sbi_port);
}

void NwdafServer::stop() {
    svr_->stop();
}

// ── PROD-06: rate-limit helper ────────────────────────────────────────────────

static bool applyRateLimit(RateLimiter& rl,
                            const httplib::Request& req,
                            httplib::Response& res)
{
    if (!rl.allow(req.remote_addr)) {
        res.set_header("Retry-After", "1");
        res.status = 429;
        res.set_content(errorResponse(429, "Too Many Requests",
            "Rate limit exceeded — retry after 1s").dump(), "application/problem+json");
        return false;
    }
    return true;
}

// ── PROD-05: correlation-ID helpers ──────────────────────────────────────────

static std::string resolveReqId(const httplib::Request& req) {
    return req.has_header("X-Request-Id")
           ? req.get_header_value("X-Request-Id")
           : generateShortId();
}

// ── Handlers ─────────────────────────────────────────────────────────────────

void NwdafServer::handleHealth(const httplib::Request& req, httplib::Response& res) {
    if (!applyRateLimit(rate_limiter_, req, res)) return;
    json body = {
        {"status",       "UP"},
        {"nfType",       "NWDAF"},
        {"nfInstanceId", config_.nf_instance_id},
        {"ts",           nowISO()},
        {"nfProfile", {
            {"nfType",       "NWDAF"},
            {"nfInstanceId", config_.nf_instance_id},
            {"nwdafInfo", {
                {"analyticsIds", {
                    "NF_LOAD", "UE_MOBILITY", "UE_COMMUNICATION",
                    "ABNORMAL_BEHAVIOUR", "QoS_SUSTAINABILITY",
                    "SERVICE_EXPERIENCE", "NETWORK_PERFORMANCE"
                }}
            }}
        }}
    };
    res.set_content(body.dump(), "application/json");
    res.status = 200;
}

void NwdafServer::handleReady(const httplib::Request& req, httplib::Response& res) {
    if (!applyRateLimit(rate_limiter_, req, res)) return;
    
    if (engine_.isReady()) {
        json body = {{"status", "READY"}};
        res.set_content(body.dump(), "application/json");
        res.status = 200;
    } else {
        json body = {{"status", "TRAINING"}, {"message", "ML models not fully fitted yet"}};
        res.set_content(body.dump(), "application/json");
        res.status = 503;
    }
}

void NwdafServer::handleGetAnalytics(const httplib::Request& req, httplib::Response& res) {
    if (!applyRateLimit(rate_limiter_, req, res)) return;

    // PROD-05: correlation ID and request timing
    std::string req_id = resolveReqId(req);
    auto t0 = std::chrono::steady_clock::now();

    auto logAndFinish = [&](const std::string& aid, const std::string& supi) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        spdlog::info("[{}] GET analytics analyticsId={} supi={} status={} latency={}ms",
                     req_id, aid.empty() ? "-" : aid,
                     supi.empty() ? "ALL" : supi, res.status, ms);
        res.set_header("X-Request-Id", req_id);
    };

    if (!req.has_param("analyticsId")) {
        res.set_content(errorResponse(400, "Missing parameter",
                                      "analyticsId is required").dump(), "application/problem+json");
        res.status = 400;
        logAndFinish("", "");
        return;
    }

    std::string analytics_id = req.get_param_value("analyticsId");

    // COMP-05: normalize the uppercase-only variant to the canonical mixed-case form
    if (analytics_id == "QOS_SUSTAINABILITY") {
        spdlog::debug("Normalizing QOS_SUSTAINABILITY → QoS_SUSTAINABILITY");
        analytics_id = "QoS_SUSTAINABILITY";
    }

    if (NwdafAnalyticsEngine::VALID_ANALYTICS_IDS.find(analytics_id) ==
        NwdafAnalyticsEngine::VALID_ANALYTICS_IDS.end())
    {
        res.set_content(errorResponse(422, "Unprocessable Entity",
                                      "Unknown analyticsId: " + analytics_id).dump(),
                        "application/problem+json");
        res.status = 422;
        logAndFinish(analytics_id, "");
        return;
    }

    std::string supi     = req.has_param("supi")    ? req.get_param_value("supi")    : "";
    std::string start_ts = req.has_param("startTs") ? req.get_param_value("startTs") : "";
    std::string end_ts   = req.has_param("endTs")   ? req.get_param_value("endTs")   : "";

    // COMP-03: NF_LOAD is aggregate-only; SUPI filtering is not applicable
    if (analytics_id == "NF_LOAD" && !supi.empty()) {
        res.set_content(errorResponse(400, "Bad Request",
            "supi parameter is not applicable for NF_LOAD analytics "
            "(NF load is network-wide, not per-UE)").dump(), "application/problem+json");
        res.status = 400;
        logAndFinish(analytics_id, supi);
        return;
    }

    try {
        std::string req_time = nowISO();
        json anal_data = engine_.compute(analytics_id, supi, start_ts, end_ts);
        int confidence = anal_data.value("confidence", 80);

        // PROD-03: update analytics-values cache for metrics scraping
        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            analytics_request_counts_[analytics_id]++;
            if (analytics_id == "ABNORMAL_BEHAVIOUR")
                metrics_cache_.anomaly_pct = anal_data.value("anomalyPct", 0.0);
            else if (analytics_id == "SERVICE_EXPERIENCE")
                metrics_cache_.mos_score   = anal_data.value("mosScore", 2.0);
            else if (analytics_id == "NETWORK_PERFORMANCE")
                metrics_cache_.network_performance_score = anal_data.value("overallScore", 0.0);
        }

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
                        "application/problem+json");
        res.status = 500;
    }
    logAndFinish(analytics_id, supi);
}

void NwdafServer::handlePostAnalytics(const httplib::Request& req, httplib::Response& res) {
    if (!applyRateLimit(rate_limiter_, req, res)) return;

    std::string req_id = resolveReqId(req);
    auto t0 = std::chrono::steady_clock::now();
    std::string analytics_id;
    std::string supi;
    
    auto logAndFinish = [&](const std::string& aid, const std::string& u) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        spdlog::info("[{}] POST analytics analyticsId={} supi={} status={} latency={}ms",
                     req_id, aid.empty() ? "-" : aid,
                     u.empty() ? "ALL" : u, res.status, ms);
        res.set_header("X-Request-Id", req_id);
    };

    try {
        json body = json::parse(req.body);
        if (!body.contains("analyticsId")) {
            res.set_content(errorResponse(400, "Missing parameter", "analyticsId is required").dump(), "application/problem+json");
            res.status = 400;
            logAndFinish("", "");
            return;
        }
        
        analytics_id = body["analyticsId"].get<std::string>();
        if (analytics_id == "QOS_SUSTAINABILITY") {
            analytics_id = "QoS_SUSTAINABILITY";
        }
        
        if (NwdafAnalyticsEngine::VALID_ANALYTICS_IDS.find(analytics_id) == NwdafAnalyticsEngine::VALID_ANALYTICS_IDS.end()) {
            res.set_content(errorResponse(422, "Unprocessable Entity", "Unknown analyticsId: " + analytics_id).dump(), "application/problem+json");
            res.status = 422;
            logAndFinish(analytics_id, "");
            return;
        }

        // P1-2: Validate filters
        if (body.contains("dnn") && body["dnn"].is_string()) {
            std::string req_dnn = body["dnn"].get<std::string>();
            if (req_dnn != config_.served_dnn) {
                res.set_content(errorResponse(400, "Bad Request", "Unsupported DNN: " + req_dnn).dump(), "application/problem+json");
                res.status = 400;
                logAndFinish(analytics_id, "");
                return;
            }
        }
        if (body.contains("snssai") && body["snssai"].is_object()) {
            auto req_snssai = body["snssai"];
            int req_sst = req_snssai.value("sst", -1);
            std::string req_sd = req_snssai.value("sd", "");
            if (req_sst != config_.served_snssai_sst || (!req_sd.empty() && req_sd != config_.served_snssai_sd)) {
                res.set_content(errorResponse(400, "Bad Request", "Unsupported S-NSSAI").dump(), "application/problem+json");
                res.status = 400;
                logAndFinish(analytics_id, "");
                return;
            }
        }

        if (body.contains("targetUeId") && body["targetUeId"].is_string()) supi = body["targetUeId"].get<std::string>();
        else if (body.contains("supi") && body["supi"].is_string()) supi = body["supi"].get<std::string>();
        
        if (analytics_id == "NF_LOAD" && !supi.empty()) {
            res.set_content(errorResponse(400, "Bad Request", "targetUeId parameter is not applicable for NF_LOAD analytics").dump(), "application/problem+json");
            res.status = 400;
            logAndFinish(analytics_id, supi);
            return;
        }

        std::string start_ts, end_ts;
        if (body.contains("anaReq") && body["anaReq"].is_object()) {
            auto req = body["anaReq"];
            if (req.contains("startTs")) start_ts = req["startTs"].get<std::string>();
            if (req.contains("endTs")) end_ts = req["endTs"].get<std::string>();
        }

        std::string req_time = nowISO();
        json anal_data = engine_.compute(analytics_id, supi, start_ts, end_ts);
        int confidence = anal_data.value("confidence", 80);

        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            analytics_request_counts_[analytics_id]++;
            if (analytics_id == "ABNORMAL_BEHAVIOUR")
                metrics_cache_.anomaly_pct = anal_data.value("anomalyPct", 0.0);
            else if (analytics_id == "SERVICE_EXPERIENCE")
                metrics_cache_.mos_score   = anal_data.value("mosScore", 2.0);
            else if (analytics_id == "NETWORK_PERFORMANCE")
                metrics_cache_.network_performance_score = anal_data.value("overallScore", 0.0);
        }

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
    } catch (const json::parse_error& e) {
        res.set_content(errorResponse(400, "Bad Request", "Invalid JSON body").dump(), "application/problem+json");
        res.status = 400;
    } catch (const std::exception& e) {
        res.set_content(errorResponse(500, "Internal Error", e.what()).dump(), "application/problem+json");
        res.status = 500;
    }
    logAndFinish(analytics_id, supi);
}

void NwdafServer::handleCreateSubscription(const httplib::Request& req, httplib::Response& res) {
    if (!applyRateLimit(rate_limiter_, req, res)) return;
    std::string req_id = resolveReqId(req);
    try {
        json body = json::parse(req.body);
        if (!body.contains("analyticsId") || !body.contains("notifUri")) {
            res.set_content(errorResponse(400, "Bad Request",
                                          "analyticsId and notifUri are required").dump(),
                            "application/problem+json");
            res.status = 400;
            res.set_header("X-Request-Id", req_id);
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
        res.set_content(errorResponse(400, "Bad Request", e.what()).dump(), "application/problem+json");
        res.status = 400;
    }
    res.set_header("X-Request-Id", req_id);
}

void NwdafServer::handleGetSubscription(const httplib::Request& req, httplib::Response& res) {
    if (!applyRateLimit(rate_limiter_, req, res)) return;
    std::string req_id = resolveReqId(req);
    std::string sub_id = req.matches[1];
    if (!subs_.exists(sub_id)) {
        res.set_content(errorResponse(404, "Not Found", "Subscription not found").dump(),
                        "application/problem+json");
        res.status = 404;
        res.set_header("X-Request-Id", req_id);
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
    res.set_header("X-Request-Id", req_id);
}

void NwdafServer::handleDeleteSubscription(const httplib::Request& req, httplib::Response& res) {
    if (!applyRateLimit(rate_limiter_, req, res)) return;
    std::string req_id = resolveReqId(req);
    std::string sub_id = req.matches[1];
    if (!subs_.remove(sub_id)) {
        res.set_content(errorResponse(404, "Not Found", "Subscription not found").dump(),
                        "application/problem+json");
        res.status = 404;
        res.set_header("X-Request-Id", req_id);
        return;
    }
    res.status = 204;
    res.set_header("X-Request-Id", req_id);
}

void NwdafServer::handleListSubscriptions(const httplib::Request& req, httplib::Response& res) {
    if (!applyRateLimit(rate_limiter_, req, res)) return;
    std::string req_id = resolveReqId(req);
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
    res.set_header("X-Request-Id", req_id);
}

void NwdafServer::handleTrainModel(const httplib::Request& req, httplib::Response& res) {
    if (!applyRateLimit(rate_limiter_, req, res)) return;
    std::string req_id = resolveReqId(req);
    try {
        json result = engine_.retrain();
        res.set_content(result.dump(), "application/json");
        res.status = 200;
    } catch (const std::exception& e) {
        res.set_content(errorResponse(500, "Training failed", e.what()).dump(), "application/problem+json");
        res.status = 500;
    }
    res.set_header("X-Request-Id", req_id);
}

// PROD-03: Prometheus text-format metrics endpoint
void NwdafServer::handleMetrics(const httplib::Request&, httplib::Response& res) {
    auto [dl_kbps, ul_kbps] = engine_.getCurrentThroughput();
    auto nf_metrics          = engine_.getCurrentNfMetrics();
    int  active_subs         = subs_.count();

    NwdafMetricsCache cache_copy;
    std::map<std::string, uint64_t> req_counts;
    {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        cache_copy  = metrics_cache_;
        req_counts  = analytics_request_counts_;
    }
    uint64_t notif_total    = notif_total_.load();
    uint64_t notif_failures = notif_failures_.load();

    std::ostringstream out;

    // Gauges
    out << "# HELP nwdaf_throughput_dl_kbps Current downlink throughput in kbps\n"
        << "# TYPE nwdaf_throughput_dl_kbps gauge\n"
        << "nwdaf_throughput_dl_kbps " << dl_kbps << "\n";

    out << "# HELP nwdaf_throughput_ul_kbps Current uplink throughput in kbps\n"
        << "# TYPE nwdaf_throughput_ul_kbps gauge\n"
        << "nwdaf_throughput_ul_kbps " << ul_kbps << "\n";

    out << "# HELP nwdaf_active_subscriptions Number of active NWDAF subscriptions\n"
        << "# TYPE nwdaf_active_subscriptions gauge\n"
        << "nwdaf_active_subscriptions " << active_subs << "\n";

    out << "# HELP nwdaf_anomaly_pct Percentage of anomalous samples (last ABNORMAL_BEHAVIOUR run)\n"
        << "# TYPE nwdaf_anomaly_pct gauge\n"
        << "nwdaf_anomaly_pct " << cache_copy.anomaly_pct << "\n";

    out << "# HELP nwdaf_mos_score MOS score from last SERVICE_EXPERIENCE run (2.0–4.5)\n"
        << "# TYPE nwdaf_mos_score gauge\n"
        << "nwdaf_mos_score " << cache_copy.mos_score << "\n";

    out << "# HELP nwdaf_network_performance_score Overall network score (0–100)\n"
        << "# TYPE nwdaf_network_performance_score gauge\n"
        << "nwdaf_network_performance_score " << cache_copy.network_performance_score << "\n";

    // Per-NF load gauges
    out << "# HELP nwdaf_nf_load_pct NF CPU load percentage\n"
        << "# TYPE nwdaf_nf_load_pct gauge\n";
    for (const auto& m : nf_metrics)
        out << "nwdaf_nf_load_pct{nf_type=\"" << m.nf_type << "\"} " << m.load_pct << "\n";

    // Counters
    out << "# HELP nwdaf_analytics_requests_total Total analytics GET requests per ID\n"
        << "# TYPE nwdaf_analytics_requests_total counter\n";
    for (const auto& [id, cnt] : req_counts)
        out << "nwdaf_analytics_requests_total{id=\"" << id << "\"} " << cnt << "\n";

    out << "# HELP nwdaf_subscription_notifications_total Total successful push notifications\n"
        << "# TYPE nwdaf_subscription_notifications_total counter\n"
        << "nwdaf_subscription_notifications_total " << notif_total << "\n";

    out << "# HELP nwdaf_subscription_notification_failures_total Failed push notifications\n"
        << "# TYPE nwdaf_subscription_notification_failures_total counter\n"
        << "nwdaf_subscription_notification_failures_total " << notif_failures << "\n";

    res.set_content(out.str(), "text/plain; version=0.0.4; charset=utf-8");
    res.status = 200;
}

// ── Traffic Simulator Handlers ────────────────────────────────────────────────

void NwdafServer::handleTrafficStart(const httplib::Request& req, httplib::Response& res) {
    try {
        auto body = nlohmann::json::parse(req.body);
        std::string type = body.value("type", "unknown");
        int duration = body.value("duration", 30);

        std::lock_guard<std::mutex> lock(traffic_mutex_);
        if (traffic_running_) {
            res.set_content(errorResponse(400, "Conflict", "Traffic already running").dump(), "application/problem+json");
            return;
        }
        traffic_running_ = true;
        current_traffic_task_ = type;

        // Spawn a detached thread to run the simulation
        std::thread([this, type, duration]() {
            int ret = 0;
            if (type == "download") {
                ret = std::system("wget -qO /dev/null http://speedtest.tele2.net/100MB.zip &");
            } else if (type == "upload_flood") {
                ret = std::system("ping -f -c 1000 8.8.8.8 > /dev/null 2>&1 &");
            } else if (type == "ping") {
                ret = std::system("ping -c 30 8.8.8.8 > /dev/null 2>&1 &");
            } else if (type == "signaling_storm") {
                // simulate
            }
            (void)ret;
            std::this_thread::sleep_for(std::chrono::seconds(duration));
            std::lock_guard<std::mutex> lock(traffic_mutex_);
            traffic_running_ = false;
            current_traffic_task_ = "none";
        }).detach();

        nlohmann::json response = {
            {"message", "Traffic simulation started"},
            {"type", type},
            {"duration", duration}
        };
        res.set_content(response.dump(), "application/json");
    } catch (...) {
        res.set_content(errorResponse(400, "Bad Request", "Invalid JSON body").dump(), "application/problem+json");
    }
}

void NwdafServer::handleTrafficStop(const httplib::Request& req, httplib::Response& res) {
    (void)req;
    std::lock_guard<std::mutex> lock(traffic_mutex_);
    traffic_running_ = false;
    current_traffic_task_ = "none";
    
    // Attempt to kill commonly used tools in simulation
    int ret = std::system("killall -q ping wget || true");
    (void)ret;
    
    nlohmann::json response = { {"message", "Traffic simulation stopped"} };
    res.set_content(response.dump(), "application/json");
}

void NwdafServer::handleTrafficStatus(const httplib::Request& req, httplib::Response& res) {
    (void)req;
    std::lock_guard<std::mutex> lock(traffic_mutex_);
    nlohmann::json response = {
        {"running", traffic_running_},
        {"current_task", current_traffic_task_}
    };
    res.set_content(response.dump(), "application/json");
}
