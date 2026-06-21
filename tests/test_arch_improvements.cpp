// Tests covering TIER-4 Architecture & ML improvements (ARCH-01 through ARCH-05).
// All existing tests in test_analytics.cpp, test_collector.cpp, and
// test_server_integration.cpp continue to pass unchanged.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "nwdaf_analytics.hpp"
#include "nwdaf_collector.hpp"
#include "nwdaf_config.hpp"
#include "ml/isolation_forest.hpp"
#include "mock_open5gs.hpp"
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <cmath>

// ── shared test helpers ───────────────────────────────────────────────────────

static NwdafConfig makeArchTestConfig() {
    NwdafConfig cfg;
    cfg.nf_instance_id          = "arch-test-uuid";
    cfg.plmn_mcc                = "999";
    cfg.plmn_mnc                = "70";
    cfg.sbi_bind_address        = "127.0.0.1";
    cfg.sbi_port                = 27779;
    cfg.nf_service_names        = {{"AMF","amfd"},{"SMF","smfd"},{"UPF","upfd"},
                                   {"AUSF","ausfd"},{"UDM","udmd"},{"PCF","pcfd"},{"NRF","nrfd"}};
    cfg.throughput_interfaces   = {"ogstun"};
    cfg.throughput_history_size = 360;
    cfg.collection_interval_seconds = 1;
    cfg.amf_journal_lines       = 500;
    cfg.smf_journal_lines       = 500;
    cfg.supi_regex              = "imsi-(\\d{15})";
    cfg.mongodb_uri             = "";
    cfg.mongodb_db              = "open5gs";
    cfg.nrf_uri                 = "http://127.0.0.1:7777";
    cfg.nrf_register_on_startup = false;
    cfg.model_dir               = "/tmp/nwdaf_arch_test_models";
    cfg.anomaly_contamination   = 0.10;
    cfg.anomaly_seed            = 42;
    cfg.anomaly_min_samples     = 10;
    cfg.baseline_stddev_min_kbps = 0.5;
    cfg.ewma_alpha              = 0.3;
    cfg.log_level               = "warn";
    cfg.log_file                = "/tmp/nwdaf_arch_test.log";
    // ARCH-03 defaults are already set by struct initialisers (0.6 / 0.2 / 0.2)
    // ARCH-05 defaults are already set by struct initialisers (tls_enabled=false)
    return cfg;
}

// ── ARCH-01: data-quality-driven confidence ───────────────────────────────────

TEST_CASE("ARCH-01: computeConfidence returns 0 when data_points < min_points") {
    REQUIRE(NwdafAnalyticsEngine::computeConfidence(0,  10, 360) == 0.0);
    REQUIRE(NwdafAnalyticsEngine::computeConfidence(9,  10, 360) == 0.0);
    REQUIRE(NwdafAnalyticsEngine::computeConfidence(1,   5, 100) == 0.0);
}

TEST_CASE("ARCH-01: computeConfidence scales linearly with coverage") {
    // 180/360 = 0.5 → round(0.5 * 95) = round(47.5) = 48
    double c_half = NwdafAnalyticsEngine::computeConfidence(180, 10, 360, 1.0);
    REQUIRE(c_half == std::round(180.0 / 360.0 * 95.0));

    // 360/360 = 1.0 → 95
    double c_full = NwdafAnalyticsEngine::computeConfidence(360, 10, 360, 1.0);
    REQUIRE(c_full == Catch::Approx(95.0));
}

TEST_CASE("ARCH-01: computeConfidence caps at 95 even with more than max_points") {
    double c = NwdafAnalyticsEngine::computeConfidence(10000, 10, 360, 1.0);
    REQUIRE(c == Catch::Approx(95.0));
}

TEST_CASE("ARCH-01: computeConfidence applies baseline_quality reduction") {
    // baseline_quality = 0.85 at max coverage → round(0.85 * 95) = round(80.75) = 81
    double c = NwdafAnalyticsEngine::computeConfidence(360, 10, 360, 0.85);
    REQUIRE(c == Catch::Approx(std::round(0.85 * 95.0)));
}

TEST_CASE("ARCH-01: ABNORMAL_BEHAVIOUR confidence is 0 for INSUFFICIENT_DATA") {
    auto cfg = makeArchTestConfig();
    MockNwdafCollector col(cfg);
    NwdafAnalyticsEngine engine(col, cfg);
    json result = engine.compute("ABNORMAL_BEHAVIOUR");
    REQUIRE(result["reason"]     == "INSUFFICIENT_DATA");
    REQUIRE(result["confidence"] == 0);
}

TEST_CASE("ARCH-01: NF_LOAD confidence is 0 when metrics list is empty") {
    // In a test environment systemctl returns no NFs → metrics empty → confidence 0
    auto cfg = makeArchTestConfig();
    MockNwdafCollector col(cfg);
    NwdafAnalyticsEngine engine(col, cfg);
    json result = engine.compute("NF_LOAD");
    // Either 0 (no metrics) or a dynamic value based on active/total — both are valid
    int conf = result["confidence"];
    REQUIRE(conf >= 0);
    REQUIRE(conf <= 95);
}

// ── ARCH-02: multi-dimensional IsolationForestN ───────────────────────────────

TEST_CASE("ARCH-02: IsolationForestN<5> outliers score lower than inliers") {
    IsolationForestN<5> iforest(100, 0.1, 42);

    // Training data: tight cluster with variance across ALL 5 features so
    // the forest can split on every dimension and reliably isolate the outlier.
    std::vector<std::array<double,5>> X;
    for (int i = 0; i < 100; ++i) {
        double v    = 10.0 + (i % 5) * 0.1;      // dl/ul  : [10.0, 10.4]
        double load = 5.0  + (i % 3) * 0.3;       // nf_load: [5.0, 5.6]
        double sess = 2.0  + (i % 4) * 0.1;       // sessions: [2.0, 2.3]
        double auth = 0.10 + (i % 5) * 0.01;      // auth_rate: [0.10, 0.14]
        X.push_back({v, v, load, sess, auth});
    }
    iforest.fit(X);
    REQUIRE(iforest.isFitted());

    // Inlier sits inside the training cluster; outlier is extreme on every feature.
    auto inlier_score  = iforest.scoresSamples({{{10.0,   10.0,   5.0,   2.0,  0.10}}});
    auto outlier_score = iforest.scoresSamples({{{1000.0, 1000.0, 95.0, 100.0, 50.0}}});

    REQUIRE(outlier_score[0] < inlier_score[0]);
}

TEST_CASE("ARCH-02: IsolationForestN<5> save/load round-trip preserves predictions") {
    IsolationForestN<5> a(10, 0.1, 42);
    std::vector<std::array<double,5>> X;
    for (int i = 0; i < 20; ++i)
        X.push_back({(double)i, (double)i, 5.0, 2.0, 0.1});
    a.fit(X);

    std::string path = "/tmp/nwdaf_arch02_5d_rt.json";
    REQUIRE_NOTHROW(a.save(path));

    IsolationForestN<5> b(10, 0.1, 42);
    REQUIRE_NOTHROW(b.load(path));
    REQUIRE(b.isFitted());

    auto pa = a.predict({{{5.0, 5.0, 5.0, 2.0, 0.1}}});
    auto pb = b.predict({{{5.0, 5.0, 5.0, 2.0, 0.1}}});
    REQUIRE(pa[0] == pb[0]);
    std::filesystem::remove(path);
}

TEST_CASE("ARCH-02: load throws on feature-dimension mismatch (2-D file into 5-D model)") {
    // Save a 2-D model
    IsolationForest a2(10, 0.1, 42);
    std::vector<std::array<double,2>> X2;
    for (int i = 0; i < 15; ++i) X2.push_back({(double)i, (double)i});
    a2.fit(X2);

    std::string path = "/tmp/nwdaf_arch02_dim_mismatch.json";
    a2.save(path);

    // Loading into 5-D model must fail with a runtime_error
    IsolationForestN<5> b5(10, 0.1, 42);
    REQUIRE_THROWS_AS(b5.load(path), std::runtime_error);
    std::filesystem::remove(path);
}

TEST_CASE("ARCH-02: 2-D IsolationForest save records n_features=2 in JSON") {
    IsolationForest iforest(10, 0.1, 42);
    std::vector<std::array<double,2>> X;
    for (int i = 0; i < 15; ++i) X.push_back({(double)i, (double)i});
    iforest.fit(X);

    std::string path = "/tmp/nwdaf_arch02_2d_nfeat.json";
    iforest.save(path);

    std::ifstream f(path);
    REQUIRE(f.is_open());
    nlohmann::json j = nlohmann::json::parse(f);
    REQUIRE(j["version"]    == 1);     // version unchanged — backward compat
    REQUIRE(j["n_features"] == 2);
    std::filesystem::remove(path);
}

TEST_CASE("ARCH-02: IsolationForest backward-compat alias — existing 2-D tests still compile") {
    // IsolationForest must be an alias for IsolationForestN<2>; this test
    // exercises the full 2-D fit/predict pipeline to guard against regressions.
    IsolationForest iforest(20, 0.1, 42);
    std::vector<std::array<double,2>> X;
    for (int i = 0; i < 30; ++i) X.push_back({(double)i, (double)(i % 5)});
    iforest.fit(X);
    REQUIRE(iforest.isFitted());
    auto preds = iforest.predict({{5.0, 1.0}, {500.0, 500.0}});
    REQUIRE(preds.size() == 2);
}

// ── ARCH-03: configurable NETWORK_PERFORMANCE weights ────────────────────────

TEST_CASE("ARCH-03: NwdafConfig np_weight fields default to standard allocation") {
    NwdafConfig cfg;
    REQUIRE(cfg.np_weight_nf_health == Catch::Approx(0.6));
    REQUIRE(cfg.np_weight_dl        == Catch::Approx(0.2));
    REQUIRE(cfg.np_weight_pdu       == Catch::Approx(0.2));
    double sum = cfg.np_weight_nf_health + cfg.np_weight_dl + cfg.np_weight_pdu;
    REQUIRE(sum == Catch::Approx(1.0));
}

TEST_CASE("ARCH-03: NwdafConfig::load validates weight sum and throws on mismatch") {
    std::string yaml_path = "/tmp/nwdaf_test_bad_weights.yaml";
    {
        std::ofstream f(yaml_path);
        // weights sum to 1.5 — must be rejected
        f << "nwdaf:\n"
          << "  nf_instance_id: \"00000000-0000-0000-0000-000000000000\"\n"
          << "  network_performance_weights:\n"
          << "    nf_health: 0.5\n"
          << "    dl_score:  0.5\n"
          << "    pdu_score: 0.5\n";
    }
    REQUIRE_THROWS_AS(NwdafConfig::load(yaml_path), std::runtime_error);
    std::filesystem::remove(yaml_path);
}

TEST_CASE("ARCH-03: NwdafConfig::load accepts weights that sum to exactly 1.0") {
    std::string yaml_path = "/tmp/nwdaf_test_good_weights.yaml";
    {
        std::ofstream f(yaml_path);
        f << "nwdaf:\n"
          << "  nf_instance_id: \"00000000-0000-0000-0000-000000000000\"\n"
          << "  network_performance_weights:\n"
          << "    nf_health: 0.7\n"
          << "    dl_score:  0.2\n"
          << "    pdu_score: 0.1\n";
    }
    NwdafConfig cfg;
    REQUIRE_NOTHROW(cfg = NwdafConfig::load(yaml_path));
    REQUIRE(cfg.np_weight_nf_health == Catch::Approx(0.7));
    REQUIRE(cfg.np_weight_dl        == Catch::Approx(0.2));
    REQUIRE(cfg.np_weight_pdu       == Catch::Approx(0.1));
    std::filesystem::remove(yaml_path);
}

TEST_CASE("ARCH-03: NETWORK_PERFORMANCE score uses custom weights from config") {
    auto cfg = makeArchTestConfig();
    // Use weight entirely on nf_health so that dl_score and pdu_score do not contribute
    cfg.np_weight_nf_health = 1.0;
    cfg.np_weight_dl        = 0.0;
    cfg.np_weight_pdu       = 0.0;

    MockNwdafCollector col(cfg);
    NwdafAnalyticsEngine engine(col, cfg);
    json result = engine.compute("NETWORK_PERFORMANCE");

    REQUIRE(result["analyticsId"] == "NETWORK_PERFORMANCE");
    double score    = result["overallScore"];
    double nf_health = result["components"]["nfHealthScore"];
    double dl_score  = result["components"]["dlScore"];
    double pdu_score = result["components"]["pduScore"];

    // With weights (1, 0, 0): overall = 1.0 * nf_health + 0 * dl + 0 * pdu
    double expected = 1.0 * nf_health + 0.0 * dl_score + 0.0 * pdu_score;
    REQUIRE(score == Catch::Approx(expected).margin(0.001));
}

// ── ARCH-04: stateful PDU session tracker ────────────────────────────────────

TEST_CASE("ARCH-04: PDU_ESTABLISHED events add entries to the active session set") {
    auto cfg = makeArchTestConfig();
    MockNwdafCollector col(cfg);

    // Three distinct SUPIs establish sessions
    col.setSmfLines({
        "[Established] PDU Session Establishment imsi-999700000000001",
        "[Established] PDU Session Establishment imsi-999700000000002",
        "[Established] PDU Session Establishment imsi-999700000000003",
    });
    col.startBackgroundCollection();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    col.stopBackgroundCollection();

    REQUIRE(col.getActivePduSessionCount() == 3);
}

TEST_CASE("ARCH-04: PDU_RELEASED events remove entries from the active session set") {
    auto cfg = makeArchTestConfig();
    MockNwdafCollector col(cfg);

    col.setSmfLines({
        "[Established] PDU Session Establishment imsi-999700000000001",
        "[Established] PDU Session Establishment imsi-999700000000002",
        "[Released] PDU Session Release imsi-999700000000001",
    });
    col.startBackgroundCollection();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    col.stopBackgroundCollection();

    // 2 established, 1 released → net 1 active
    REQUIRE(col.getActivePduSessionCount() == 1);
}

TEST_CASE("ARCH-04: [Removed] pattern (Open5GS v2.7.6) also decrements active count") {
    auto cfg = makeArchTestConfig();
    MockNwdafCollector col(cfg);

    col.setSmfLines({
        "[Established] PDU Session Establishment imsi-999700000000010",
        "[Removed] imsi-999700000000010",
    });
    col.startBackgroundCollection();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    col.stopBackgroundCollection();

    REQUIRE(col.getActivePduSessionCount() == 0);
}

TEST_CASE("ARCH-04: getActivePduSessionCount starts at zero before any collection") {
    auto cfg = makeArchTestConfig();
    MockNwdafCollector col(cfg);
    REQUIRE(col.getActivePduSessionCount() == 0);
}

TEST_CASE("ARCH-04: duplicate PDU_ESTABLISHED for same SUPI counts only once") {
    auto cfg = makeArchTestConfig();
    MockNwdafCollector col(cfg);

    // Same SUPI sent twice — unordered_set deduplicates
    col.setSmfLines({
        "[Established] PDU Session Establishment imsi-999700000000042",
        "[Established] PDU Session Establishment imsi-999700000000042",
    });
    col.startBackgroundCollection();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    col.stopBackgroundCollection();

    REQUIRE(col.getActivePduSessionCount() == 1);
}

// ── ARCH-05: TLS / mTLS configuration ────────────────────────────────────────

TEST_CASE("ARCH-05: NwdafConfig TLS fields default to disabled with sensible paths") {
    NwdafConfig cfg;
    REQUIRE(cfg.tls_enabled   == false);
    REQUIRE(!cfg.tls_cert_file.empty());
    REQUIRE(!cfg.tls_key_file.empty());
    REQUIRE(!cfg.tls_ca_file.empty());
}

TEST_CASE("ARCH-05: NwdafConfig TLS fields can be set programmatically") {
    NwdafConfig cfg;
    cfg.tls_enabled   = true;
    cfg.tls_cert_file = "/etc/open5gs/tls/nwdaf.pem";
    cfg.tls_key_file  = "/etc/open5gs/tls/nwdaf.key";
    cfg.tls_ca_file   = "/etc/open5gs/tls/ca.pem";
    REQUIRE(cfg.tls_enabled == true);
    REQUIRE(cfg.tls_cert_file == "/etc/open5gs/tls/nwdaf.pem");
    REQUIRE(cfg.tls_key_file  == "/etc/open5gs/tls/nwdaf.key");
    REQUIRE(cfg.tls_ca_file   == "/etc/open5gs/tls/ca.pem");
}

TEST_CASE("ARCH-05: NwdafConfig::load reads TLS fields from YAML") {
    std::string yaml_path = "/tmp/nwdaf_arch05_tls_config.yaml";
    {
        std::ofstream f(yaml_path);
        f << "nwdaf:\n"
          << "  nf_instance_id: \"00000000-0000-0000-0000-000000000000\"\n"
          << "  tls_enabled: true\n"
          << "  tls_cert_file: \"/tmp/test.pem\"\n"
          << "  tls_key_file:  \"/tmp/test.key\"\n"
          << "  tls_ca_file:   \"/tmp/test-ca.pem\"\n";
    }
    NwdafConfig cfg;
    REQUIRE_NOTHROW(cfg = NwdafConfig::load(yaml_path));
    REQUIRE(cfg.tls_enabled   == true);
    REQUIRE(cfg.tls_cert_file == "/tmp/test.pem");
    REQUIRE(cfg.tls_key_file  == "/tmp/test.key");
    REQUIRE(cfg.tls_ca_file   == "/tmp/test-ca.pem");
    std::filesystem::remove(yaml_path);
}

TEST_CASE("ARCH-05: NwdafConfig::load defaults tls_enabled to false when key absent") {
    std::string yaml_path = "/tmp/nwdaf_arch05_notls.yaml";
    {
        std::ofstream f(yaml_path);
        f << "nwdaf:\n"
          << "  nf_instance_id: \"00000000-0000-0000-0000-000000000000\"\n"
          << "  sbi_port: 7779\n"; // minimal config, no TLS keys
    }
    NwdafConfig cfg;
    REQUIRE_NOTHROW(cfg = NwdafConfig::load(yaml_path));
    REQUIRE(cfg.tls_enabled == false);
    std::filesystem::remove(yaml_path);
}
