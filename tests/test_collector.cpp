#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "nwdaf_collector.hpp"
#include "mock_open5gs.hpp"
#include <thread>
#include <chrono>

static NwdafConfig makeTestConfig() {
    NwdafConfig cfg;
    cfg.nf_instance_id = "test-collector";
    cfg.plmn_mcc = "999"; cfg.plmn_mnc = "70";
    cfg.sbi_bind_address = "127.0.0.1"; cfg.sbi_port = 17779;
    cfg.nf_service_names = {{"AMF","amfd"},{"SMF","smfd"},{"UPF","upfd"}};
    cfg.throughput_interfaces = {"ogstun"};
    cfg.throughput_history_size = 360;
    cfg.collection_interval_seconds = 1;
    cfg.amf_journal_lines = 50; cfg.smf_journal_lines = 50;
    cfg.supi_regex = "imsi-(\\d{15})";
    cfg.mongodb_uri = ""; cfg.mongodb_db = "open5gs";
    cfg.nrf_uri = "http://127.0.0.1:7777"; cfg.nrf_register_on_startup = false;
    cfg.model_dir = "/tmp"; cfg.anomaly_contamination = 0.10;
    cfg.anomaly_min_samples = 10; cfg.baseline_stddev_min_kbps = 0.5;
    cfg.ewma_alpha = 0.3;
    cfg.log_level = "warn"; cfg.log_file = "/tmp/nwdaf_test.log";
    return cfg;
}

TEST_CASE("MockCollector: AMF event parsing") {
    auto cfg = makeTestConfig();
    MockNwdafCollector col(cfg);
    col.setAmfLines({
        "Apr 20 10:00:01 host open5gs-amfd[123]: Registration imsi-999700000000001 accepted",
        "Apr 20 10:00:02 host open5gs-amfd[123]: Deregistration imsi-999700000000002",
        "Apr 20 10:00:03 host open5gs-amfd[123]: Handover imsi-999700000000001",
    });

    auto events = col.collectAmfEvents();
    REQUIRE(events.size() >= 2);
    bool found_reg = false;
    for (const auto& e : events)
        if (e.event_type == "REGISTRATION") { found_reg = true; break; }
    REQUIRE(found_reg);
}

TEST_CASE("MockCollector: SMF event parsing") {
    auto cfg = makeTestConfig();
    MockNwdafCollector col(cfg);
    col.setSmfLines({
        "[Established] PDU Session Establishment",
        "[Released] PDU Session Release",
        "QoS Flow modification"
    });

    auto events = col.collectSmfEvents();
    REQUIRE(events.size() >= 2);
}

TEST_CASE("MockCollector: subscriber count") {
    auto cfg = makeTestConfig();
    MockNwdafCollector col(cfg);
    col.setSubscriberCount(42);
    REQUIRE(col.getSubscriberCount() == 42);
}

TEST_CASE("MockCollector: background collection stores throughput history") {
    auto cfg = makeTestConfig();
    cfg.collection_interval_seconds = 1;
    MockNwdafCollector col(cfg);
    col.setNetStats("ogstun", 100000, 50000);

    col.startBackgroundCollection();
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    col.stopBackgroundCollection();

    auto hist = col.getThroughputHistory(10);
    REQUIRE(hist.size() >= 1);
}

TEST_CASE("MockCollector: CPU load is rate-based, not cumulative") {
    auto cfg = makeTestConfig();
    MockNwdafCollector col(cfg);

    auto t0 = std::chrono::steady_clock::now();
    col.setMockCpuTime(t0);

    // First call: no snapshot exists → establishes baseline, returns 0.0
    col.setProcStat(1000, 0, 0);
    double r1 = col.testComputeCpuPct(1000);
    REQUIRE(r1 == 0.0);

    // Advance 1 second, process consumed 50 ticks (utime+stime).
    // Expected: 100 × 50 / (1.0 × 100 Hz) = 50 %
    col.advanceMockCpuTime(std::chrono::seconds(1));
    col.setProcStat(1000, 50, 0);
    double r2 = col.testComputeCpuPct(1000);
    REQUIRE(r2 == Catch::Approx(50.0).epsilon(0.01));

    // Third call with zero new ticks → 0 %
    col.advanceMockCpuTime(std::chrono::seconds(1));
    // proc stat unchanged — same (50, 0)
    double r3 = col.testComputeCpuPct(1000);
    REQUIRE(r3 == 0.0);
}

TEST_CASE("NwdafCollector: throughput sample has valid fields") {
    auto cfg = makeTestConfig();
    MockNwdafCollector col(cfg);
    col.setNetStats("ogstun", 0, 0);

    auto sample = col.collectUPFThroughput();
    REQUIRE(!sample.timestamp_iso.empty());
    REQUIRE(sample.total_dl_bps >= 0);
    REQUIRE(sample.total_ul_bps >= 0);
    REQUIRE(sample.total_dl_kbps >= 0);
    REQUIRE(sample.total_ul_kbps >= 0);
}
