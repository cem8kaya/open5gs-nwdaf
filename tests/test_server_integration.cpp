#include <catch2/catch_test_macros.hpp>
#include "nwdaf_server.hpp"
#include "nwdaf_analytics.hpp"
#include "nwdaf_subscription.hpp"
#include "mock_open5gs.hpp"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <memory>

static const int TEST_PORT = 17779;

static NwdafConfig makeTestConfig() {
    NwdafConfig cfg;
    cfg.nf_instance_id = "test-server-uuid";
    cfg.plmn_mcc = "999"; cfg.plmn_mnc = "70";
    cfg.sbi_bind_address = "127.0.0.1"; cfg.sbi_port = TEST_PORT;
    cfg.nf_service_names = {{"AMF","amfd"},{"SMF","smfd"},{"UPF","upfd"},
                             {"AUSF","ausfd"},{"UDM","udmd"},{"PCF","pcfd"},{"NRF","nrfd"}};
    cfg.throughput_interfaces = {"ogstun"};
    cfg.throughput_history_size = 360;
    cfg.collection_interval_seconds = 10;
    cfg.amf_journal_lines = 50; cfg.smf_journal_lines = 50;
    cfg.supi_regex = "imsi-(\\d{15})";
    cfg.mongodb_uri = ""; cfg.mongodb_db = "open5gs";
    cfg.nrf_uri = "http://127.0.0.1:7777"; cfg.nrf_register_on_startup = false;
    cfg.model_dir = "/tmp/nwdaf_test_models";
    cfg.anomaly_contamination = 0.10; cfg.anomaly_min_samples = 10;
    cfg.baseline_stddev_min_kbps = 0.5; cfg.ewma_alpha = 0.3;
    cfg.log_level = "warn"; cfg.log_file = "/tmp/nwdaf_test.log";
    return cfg;
}

// Server fixture shared across all integration tests
struct ServerFixture {
    NwdafConfig               cfg;
    MockNwdafCollector        collector;
    NwdafAnalyticsEngine      engine;
    NwdafSubscriptionStore    subs;
    NwdafServer               server;
    std::thread               server_thread;

    ServerFixture()
        : cfg(makeTestConfig()),
          collector(cfg),
          engine(collector, cfg),
          server(engine, subs, cfg)
    {
        server_thread = std::thread([this]{ server.start(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    ~ServerFixture() {
        server.stop();
        if (server_thread.joinable()) server_thread.join();
    }
};

static std::unique_ptr<ServerFixture> g_fixture;

// One-time setup/teardown not easily done with Catch2 — use lazy init per TEST_CASE
static ServerFixture& getFixture() {
    if (!g_fixture) g_fixture = std::make_unique<ServerFixture>();
    return *g_fixture;
}

TEST_CASE("Integration: GET /health returns 200 UP") {
    auto& fx = getFixture();
    httplib::Client cli("127.0.0.1", TEST_PORT);
    cli.set_connection_timeout(3);
    auto res = cli.Get("/nwdaf-analytics/v1/health");
    REQUIRE(res);
    REQUIRE(res->status == 200);
    json body = json::parse(res->body);
    REQUIRE(body["status"] == "UP");
}

TEST_CASE("Integration: GET /analytics?analyticsId=NF_LOAD returns 200") {
    auto& fx = getFixture();
    httplib::Client cli("127.0.0.1", TEST_PORT);
    auto res = cli.Get("/nwdaf-analytics/v1/analytics?analyticsId=NF_LOAD");
    REQUIRE(res);
    REQUIRE(res->status == 200);
    json body = json::parse(res->body);
    REQUIRE(body["analData"].contains("nfLoadLevelList"));
}

TEST_CASE("Integration: GET /analytics without param returns 400") {
    auto& fx = getFixture();
    httplib::Client cli("127.0.0.1", TEST_PORT);
    auto res = cli.Get("/nwdaf-analytics/v1/analytics");
    REQUIRE(res);
    REQUIRE(res->status == 400);
}

TEST_CASE("Integration: GET /analytics?analyticsId=INVALID returns 422") {
    auto& fx = getFixture();
    httplib::Client cli("127.0.0.1", TEST_PORT);
    auto res = cli.Get("/nwdaf-analytics/v1/analytics?analyticsId=INVALID_ID");
    REQUIRE(res);
    REQUIRE(res->status == 422);
}

TEST_CASE("Integration: POST /subscriptions creates subscription with subId") {
    auto& fx = getFixture();
    httplib::Client cli("127.0.0.1", TEST_PORT);
    json body = {{"analyticsId","NF_LOAD"},{"notifUri","http://127.0.0.1:9999/callback"},
                 {"repPeriod", 30}};
    auto res = cli.Post("/nwdaf-analytics/v1/subscriptions", body.dump(), "application/json");
    REQUIRE(res);
    REQUIRE(res->status == 201);
    json resp = json::parse(res->body);
    REQUIRE(resp.contains("subId"));
}

TEST_CASE("Integration: GET /subscriptions/<subId> returns 200") {
    auto& fx = getFixture();
    httplib::Client cli("127.0.0.1", TEST_PORT);

    // Create first
    json body = {{"analyticsId","UE_MOBILITY"},{"notifUri","http://127.0.0.1:9999/cb"}};
    auto create_res = cli.Post("/nwdaf-analytics/v1/subscriptions", body.dump(), "application/json");
    REQUIRE(create_res);
    std::string sub_id = json::parse(create_res->body)["subId"];

    auto res = cli.Get("/nwdaf-analytics/v1/subscriptions/" + sub_id);
    REQUIRE(res);
    REQUIRE(res->status == 200);
    json resp = json::parse(res->body);
    REQUIRE(resp["subId"] == sub_id);
}

TEST_CASE("Integration: DELETE /subscriptions/<subId> returns 204") {
    auto& fx = getFixture();
    httplib::Client cli("127.0.0.1", TEST_PORT);

    json body = {{"analyticsId","NF_LOAD"},{"notifUri","http://127.0.0.1:9999/del"}};
    auto create_res = cli.Post("/nwdaf-analytics/v1/subscriptions", body.dump(), "application/json");
    std::string sub_id = json::parse(create_res->body)["subId"];

    auto del_res = cli.Delete("/nwdaf-analytics/v1/subscriptions/" + sub_id);
    REQUIRE(del_res);
    REQUIRE(del_res->status == 204);
}

TEST_CASE("Integration: GET /subscriptions/<subId> after delete returns 404") {
    auto& fx = getFixture();
    httplib::Client cli("127.0.0.1", TEST_PORT);

    json body = {{"analyticsId","NF_LOAD"},{"notifUri","http://127.0.0.1:9999/404"}};
    auto create_res = cli.Post("/nwdaf-analytics/v1/subscriptions", body.dump(), "application/json");
    std::string sub_id = json::parse(create_res->body)["subId"];
    cli.Delete("/nwdaf-analytics/v1/subscriptions/" + sub_id);

    auto res = cli.Get("/nwdaf-analytics/v1/subscriptions/" + sub_id);
    REQUIRE(res);
    REQUIRE(res->status == 404);
}

TEST_CASE("Integration: All 7 analytics IDs return 200 with correct analyticsId") {
    auto& fx = getFixture();
    httplib::Client cli("127.0.0.1", TEST_PORT);

    std::vector<std::string> ids = {
        "NF_LOAD", "UE_MOBILITY", "UE_COMMUNICATION",
        "ABNORMAL_BEHAVIOUR", "QoS_SUSTAINABILITY",
        "SERVICE_EXPERIENCE", "NETWORK_PERFORMANCE"
    };
    for (const auto& id : ids) {
        auto res = cli.Get("/nwdaf-analytics/v1/analytics?analyticsId=" + id);
        REQUIRE(res);
        REQUIRE(res->status == 200);
        json body = json::parse(res->body);
        REQUIRE(body["analyticsId"] == id);
    }
}

TEST_CASE("Integration: POST /train returns 200") {
    auto& fx = getFixture();
    httplib::Client cli("127.0.0.1", TEST_PORT);
    auto res = cli.Post("/nwdaf-analytics/v1/train", "", "application/json");
    REQUIRE(res);
    REQUIRE(res->status == 200);
}

// ── COMP-05: QOS_SUSTAINABILITY case normalization ────────────────────────────

TEST_CASE("Integration: COMP-05 QOS_SUSTAINABILITY (uppercase) normalized to 200") {
    auto& fx = getFixture();
    httplib::Client cli("127.0.0.1", TEST_PORT);
    // Canonical casing
    auto res1 = cli.Get("/nwdaf-analytics/v1/analytics?analyticsId=QoS_SUSTAINABILITY");
    REQUIRE(res1);
    REQUIRE(res1->status == 200);
    json b1 = json::parse(res1->body);
    REQUIRE(b1["analyticsId"] == "QoS_SUSTAINABILITY");

    // Uppercase variant — must be normalized, not rejected with 422
    auto res2 = cli.Get("/nwdaf-analytics/v1/analytics?analyticsId=QOS_SUSTAINABILITY");
    REQUIRE(res2);
    REQUIRE(res2->status == 200);
    json b2 = json::parse(res2->body);
    // Server normalizes to canonical form in the response
    REQUIRE(b2["analyticsId"] == "QoS_SUSTAINABILITY");
}

// ── COMP-03: NF_LOAD rejects supi parameter ──────────────────────────────────

TEST_CASE("Integration: COMP-03 NF_LOAD with supi returns 400") {
    auto& fx = getFixture();
    httplib::Client cli("127.0.0.1", TEST_PORT);
    auto res = cli.Get("/nwdaf-analytics/v1/analytics?analyticsId=NF_LOAD&supi=imsi-999700000000001");
    REQUIRE(res);
    REQUIRE(res->status == 400);
    json body = json::parse(res->body);
    REQUIRE(body.contains("cause"));
}

// ── COMP-03: UE_COMMUNICATION returns supi field when filtered ─────────────────

TEST_CASE("Integration: COMP-03 UE_COMMUNICATION with supi includes supi in response") {
    auto& fx = getFixture();
    httplib::Client cli("127.0.0.1", TEST_PORT);
    auto res = cli.Get("/nwdaf-analytics/v1/analytics"
                       "?analyticsId=UE_COMMUNICATION&supi=imsi-999700000000001");
    REQUIRE(res);
    REQUIRE(res->status == 200);
    json body = json::parse(res->body);
    REQUIRE(body["analData"].contains("supi"));
    REQUIRE(body["analData"]["supi"] == "imsi-999700000000001");
}

// ── COMP-03: QoS_SUSTAINABILITY with supi returns supiFiltered flag ───────────

TEST_CASE("Integration: COMP-03 QoS_SUSTAINABILITY with supi returns supiFiltered=false") {
    auto& fx = getFixture();
    httplib::Client cli("127.0.0.1", TEST_PORT);
    auto res = cli.Get("/nwdaf-analytics/v1/analytics"
                       "?analyticsId=QoS_SUSTAINABILITY&supi=imsi-999700000000001");
    REQUIRE(res);
    REQUIRE(res->status == 200);
    json body = json::parse(res->body);
    REQUIRE(body["analData"].contains("supiFiltered"));
    REQUIRE(body["analData"]["supiFiltered"] == false);
}
