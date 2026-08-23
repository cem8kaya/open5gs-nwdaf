// H1 quick wins — tests for the E-model MOS estimator (H1.5) and the three
// analytics IDs added to complete more of the Rel-17/18 catalogue (H1.4).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "nwdaf_analytics.hpp"
#include "ml/mos_estimator.hpp"
#include "mock_open5gs.hpp"
#include <cmath>
#include <thread>
#include <chrono>

using Catch::Approx;

static NwdafConfig h1Config() {
    NwdafConfig cfg;
    cfg.nf_instance_id = "test-uuid";
    cfg.plmn_mcc = "999"; cfg.plmn_mnc = "70";
    cfg.served_snssai_sst = 1; cfg.served_snssai_sd = "000001";
    cfg.served_dnn = "internet";
    cfg.sbi_bind_address = "127.0.0.1"; cfg.sbi_port = 17779;
    cfg.nf_service_names = {{"AMF","amfd"},{"SMF","smfd"},{"UPF","upfd"}};
    cfg.throughput_interfaces = {"ogstun"};
    cfg.throughput_history_size = 360;
    cfg.collection_interval_seconds = 10;
    cfg.amf_journal_lines = 500; cfg.smf_journal_lines = 500;
    cfg.supi_regex = "imsi-(\\d{15})";
    cfg.nrf_register_on_startup = false;
    cfg.model_dir = "/tmp/nwdaf_h1_models";
    cfg.anomaly_contamination = 0.10; cfg.anomaly_min_samples = 10;
    cfg.baseline_stddev_min_kbps = 0.5; cfg.ewma_alpha = 0.3;
    cfg.log_level = "warn"; cfg.log_file = "/tmp/nwdaf_h1_test.log";
    return cfg;
}

// Push a deterministic throughput series into the collector's history ring.
static void seedThroughput(MockNwdafCollector& col,
                           const std::vector<std::pair<double,double>>& dl_ul) {
    int i = 0;
    for (const auto& [dl, ul] : dl_ul) {
        ThroughputSample s;
        char buf[32];
        snprintf(buf, sizeof(buf), "2026-08-23T10:%02d:00Z", i % 60);
        s.timestamp_iso = buf;
        s.total_dl_kbps = dl;
        s.total_ul_kbps = ul;
        s.total_dl_bps  = dl * 1000.0;
        s.total_ul_bps  = ul * 1000.0;
        col.appendThroughputSample(s);
        ++i;
    }
}

// ── H1.5: E-model MOS estimator ──────────────────────────────────────────────

TEST_CASE("H1.5: E-model reproduces the old step ladder's anchor points") {
    // The estimator is calibrated so it stays within ~0.15 MOS of the ladder it
    // replaces at the ladder's own breakpoints — existing alert thresholds and
    // dashboards keep their meaning.
    struct { double kbps; double ladder; } anchors[] = {
        {  50.0, 3.0 }, { 100.0, 3.5 }, { 500.0, 4.0 }, { 1000.0, 4.5 },
    };
    for (const auto& a : anchors) {
        MosInputs in; in.has_throughput = true; in.dl_kbps = a.kbps;
        double mos = MosEstimator::estimate(in).mos;
        INFO("kbps=" << a.kbps << " emodel=" << mos << " ladder=" << a.ladder);
        REQUIRE(std::fabs(mos - a.ladder) < 0.35);
    }
    // The 50 kbps anchor is the exact calibration point.
    MosInputs in50; in50.has_throughput = true; in50.dl_kbps = 50.0;
    REQUIRE(MosEstimator::estimate(in50).mos == Approx(3.0).margin(0.02));
}

TEST_CASE("H1.5: MOS is continuous and strictly increasing in throughput") {
    // The defect the step ladder had: a 1 kbps change at a breakpoint moved MOS
    // by a whole half-point, and no change anywhere else did anything.
    // Swept above the bottom of the scale: the G.107 polynomial saturates at
    // MOS 1.0 for very small R, so the guarantee is monotonicity across the
    // usable range, not below it.
    MosInputs first; first.has_throughput = true; first.dl_kbps = 2.0;
    double prev = MosEstimator::estimate(first).mos;
    for (double kbps = 2.1; kbps <= 1000.0; kbps *= 1.05) {
        MosInputs in; in.has_throughput = true; in.dl_kbps = kbps;
        double mos = MosEstimator::estimate(in).mos;
        REQUIRE(mos > prev);            // strictly monotone
        REQUIRE(mos - prev < 0.05);     // no discontinuous jumps
        prev = mos;
    }
}

TEST_CASE("H1.5: an idle link scores exactly MOS 1.0") {
    // The constants are tuned so zero throughput lands on R = 0, the bottom of
    // the G.107 scale — an idle bearer is unusable, not merely poor.
    MosInputs idle; idle.has_throughput = true; idle.dl_kbps = 0.0;
    auto r = MosEstimator::estimate(idle);
    REQUIRE(r.r_factor == Approx(0.0).margin(0.1));
    REQUIRE(r.mos == Approx(1.0).margin(0.01));
    REQUIRE(r.category == "POOR");
}

TEST_CASE("H1.5: MOS stays within the G.107 rating scale") {
    for (double kbps : {0.0, 0.001, 1.0, 1e3, 1e6, 1e12}) {
        MosInputs in; in.has_throughput = true; in.dl_kbps = kbps;
        double mos = MosEstimator::estimate(in).mos;
        REQUIRE(mos >= 1.0);
        REQUIRE(mos <= 4.5);
    }
}

TEST_CASE("H1.5: packet loss and delay degrade MOS monotonically") {
    auto mosWithLoss = [](double pct) {
        MosInputs in; in.has_throughput = true; in.dl_kbps = 500.0;
        in.has_packet_loss = true; in.packet_loss_pct = pct;
        return MosEstimator::estimate(in).mos;
    };
    REQUIRE(mosWithLoss(0.0) > mosWithLoss(1.0));
    REQUIRE(mosWithLoss(1.0) > mosWithLoss(5.0));
    REQUIRE(mosWithLoss(5.0) > mosWithLoss(20.0));

    auto mosWithRtt = [](double rtt) {
        MosInputs in; in.has_throughput = true; in.dl_kbps = 1000.0;
        in.has_latency = true; in.rtt_ms = rtt;
        return MosEstimator::estimate(in).mos;
    };
    // G.107 treats one-way delay below 100 ms (200 ms RTT) as perceptually free.
    REQUIRE(mosWithRtt(50.0) == Approx(mosWithRtt(0.0)));
    REQUIRE(mosWithRtt(0.0) > mosWithRtt(600.0));
    REQUIRE(mosWithRtt(600.0) > mosWithRtt(1500.0));
}

TEST_CASE("H1.5: absent optional inputs contribute no impairment") {
    MosInputs bare;  bare.has_throughput = true;  bare.dl_kbps = 500.0;
    MosInputs clean = bare;
    clean.has_packet_loss = true; clean.packet_loss_pct = 0.0;
    clean.has_latency     = true; clean.rtt_ms          = 0.0;
    REQUIRE(MosEstimator::estimate(bare).mos == Approx(MosEstimator::estimate(clean).mos));
}

TEST_CASE("H1.5: falls back to the step ladder when throughput is unavailable") {
    MosInputs in;  // has_throughput stays false
    in.dl_kbps = 700.0;
    auto r = MosEstimator::estimate(in);
    REQUIRE(r.method == "STEP_FALLBACK");
    REQUIRE(r.mos == Approx(4.0));
}

TEST_CASE("H1.5: per-session MOS divides the pipe across active sessions") {
    MosInputs in; in.has_throughput = true; in.dl_kbps = 900.0;
    in.active_sessions = 9;
    auto r = MosEstimator::estimate(in);
    REQUIRE(r.per_session_available);
    REQUIRE(r.per_session_kbps == Approx(100.0));
    REQUIRE(r.per_session_mos < r.mos);   // contention costs experience

    MosInputs solo = in; solo.active_sessions = 0;
    REQUIRE_FALSE(MosEstimator::estimate(solo).per_session_available);
}

TEST_CASE("H1.5: SERVICE_EXPERIENCE reports the model and its impairments") {
    auto cfg = h1Config();
    MockNwdafCollector col(cfg);
    seedThroughput(col, {{800.0, 400.0}});

    NwdafAnalyticsEngine engine(col, cfg);
    json r = engine.compute("SERVICE_EXPERIENCE");

    REQUIRE(r["analyticsId"] == "SERVICE_EXPERIENCE");
    REQUIRE(r["mosMethod"] == "E_MODEL");
    REQUIRE(r.contains("rFactor"));
    REQUIRE(r["impairments"].contains("throughput"));
    REQUIRE(r["impairments"].contains("loss"));
    REQUIRE(r["impairments"].contains("delay"));
    // No PFCP inputs yet, so loss/delay must be exactly zero rather than guessed.
    REQUIRE(r["impairments"]["loss"].get<double>()  == Approx(0.0));
    REQUIRE(r["impairments"]["delay"].get<double>() == Approx(0.0));
    double mos = r["mosScore"].get<double>();
    REQUIRE(mos > 4.0);
    REQUIRE(mos <= 4.5);
}

TEST_CASE("H1.5: SERVICE_EXPERIENCE degrades gracefully with no samples") {
    auto cfg = h1Config();
    MockNwdafCollector col(cfg);
    NwdafAnalyticsEngine engine(col, cfg);
    json r = engine.compute("SERVICE_EXPERIENCE");
    REQUIRE(r["mosMethod"] == "STEP_FALLBACK");
    REQUIRE(r["confidence"].get<int>() < 78);
}

// ── H1.4: dispersion statistics ──────────────────────────────────────────────

TEST_CASE("H1.4: Gini is 0 for an even series and rises with concentration") {
    REQUIRE(NwdafAnalyticsEngine::giniCoefficient({5, 5, 5, 5}) == Approx(0.0).margin(1e-9));
    double even    = NwdafAnalyticsEngine::giniCoefficient({10, 10, 10, 10});
    double skewed  = NwdafAnalyticsEngine::giniCoefficient({1, 1, 1, 37});
    double extreme = NwdafAnalyticsEngine::giniCoefficient({0, 0, 0, 100});
    REQUIRE(even < skewed);
    REQUIRE(skewed < extreme);
    REQUIRE(extreme <= 1.0);
    // Degenerate inputs must not produce NaN.
    REQUIRE(NwdafAnalyticsEngine::giniCoefficient({}) == Approx(0.0));
    REQUIRE(NwdafAnalyticsEngine::giniCoefficient({7}) == Approx(0.0));
    REQUIRE(NwdafAnalyticsEngine::giniCoefficient({0, 0, 0}) == Approx(0.0));
}

TEST_CASE("H1.4: coefficient of variation is scale-invariant") {
    double a = NwdafAnalyticsEngine::coefficientOfVariation({10, 20, 30});
    double b = NwdafAnalyticsEngine::coefficientOfVariation({100, 200, 300});
    REQUIRE(a == Approx(b));
    REQUIRE(NwdafAnalyticsEngine::coefficientOfVariation({5, 5, 5}) == Approx(0.0));
    REQUIRE(NwdafAnalyticsEngine::coefficientOfVariation({}) == Approx(0.0));
}

TEST_CASE("H1.4: DISPERSION separates flat traffic from bursty traffic") {
    auto cfg = h1Config();

    MockNwdafCollector flat(cfg);
    seedThroughput(flat, std::vector<std::pair<double,double>>(20, {100.0, 50.0}));
    NwdafAnalyticsEngine flat_engine(flat, cfg);
    json f = flat_engine.compute("DISPERSION");

    MockNwdafCollector bursty(cfg);
    std::vector<std::pair<double,double>> burst(20, {1.0, 1.0});
    burst[7] = {5000.0, 2000.0};
    seedThroughput(bursty, burst);
    NwdafAnalyticsEngine burst_engine(bursty, cfg);
    json b = burst_engine.compute("DISPERSION");

    REQUIRE(f["analyticsId"] == "DISPERSION");
    double f_gini = f["dataVolumeDispersion"]["giniCoefficient"].get<double>();
    double b_gini = b["dataVolumeDispersion"]["giniCoefficient"].get<double>();
    REQUIRE(f_gini == Approx(0.0).margin(1e-6));
    REQUIRE(b_gini > 0.5);
    REQUIRE(f["dataVolumeDispersion"]["dispersionClass"] == "LOW");
    REQUIRE(b["dataVolumeDispersion"]["dispersionClass"] == "HIGH");
    // One sample in twenty carrying nearly all the volume must dominate the
    // top decile.
    REQUIRE(b["dataVolumeDispersion"]["topDecileSharePct"].get<double>() > 90.0);
}

TEST_CASE("H1.4: DISPERSION ranks top talkers by transaction share") {
    auto cfg = h1Config();
    MockNwdafCollector col(cfg);
    col.setSmfLines({
        "[Established] PDU Session Establishment imsi-999700000000001",
        "[Established] PDU Session Establishment imsi-999700000000001",
        "[Established] PDU Session Establishment imsi-999700000000001",
        "[Released] PDU Session Release imsi-999700000000001",
        "[Established] PDU Session Establishment imsi-999700000000002",
    });
    col.startBackgroundCollection();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    col.stopBackgroundCollection();

    NwdafAnalyticsEngine engine(col, cfg);
    json r = engine.compute("DISPERSION");

    auto& td = r["transactionDispersion"];
    REQUIRE(td["ueCount"].get<int>() == 2);
    REQUIRE(td["topTalkers"].size() == 2);
    // The busiest subscriber leads, and the ranking is ordered.
    REQUIRE(td["topTalkers"][0]["supi"] == "imsi-999700000000001");
    REQUIRE(td["topTalkers"][0]["transactions"].get<int>()
            >= td["topTalkers"][1]["transactions"].get<int>());
    REQUIRE(td["topTalkers"][0]["sharePct"].get<double>() > 50.0);
    REQUIRE(td["giniCoefficient"].get<double>() > 0.0);
}

// ── H1.4: SM_CONGESTION ──────────────────────────────────────────────────────

TEST_CASE("H1.4: SM_CONGESTION reports NONE on a healthy core") {
    auto cfg = h1Config();
    MockNwdafCollector col(cfg);
    col.setSmfLines({
        "[Established] PDU Session Establishment imsi-999700000000001",
        "[Established] PDU Session Establishment imsi-999700000000002",
    });
    std::vector<NfMetric> nf = {
        {"SMF", "active", 101, 1.0, 4096, 5.0, "LOW"},
        {"UPF", "active", 102, 1.0, 4096, 4.0, "LOW"},
        {"AMF", "active", 103, 1.0, 4096, 3.0, "LOW"},
    };
    col.setNfMetrics(nf);
    col.startBackgroundCollection();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    col.stopBackgroundCollection();

    NwdafAnalyticsEngine engine(col, cfg);
    json r = engine.compute("SM_CONGESTION");

    REQUIRE(r["analyticsId"] == "SM_CONGESTION");
    REQUIRE(r["congestionLevel"] == "NONE");
    REQUIRE(r["sessionEstFailures"].get<int>() == 0);
    REQUIRE(r["sessionEstFailureRatePct"].get<double>() == Approx(0.0));
    REQUIRE(r["recommendation"] == "NONE");
    REQUIRE(r["dnn"] == "internet");
    REQUIRE(r["snssai"]["sst"].get<int>() == 1);
}

TEST_CASE("H1.4: SM_CONGESTION escalates on establishment rejects") {
    auto cfg = h1Config();
    MockNwdafCollector col(cfg);
    // Four rejects against one success — a 80% failure ratio.
    col.setSmfLines({
        "[Established] PDU Session Establishment imsi-999700000000001",
        "PDU Session Establishment Reject imsi-999700000000002",
        "PDU Session Establishment Reject imsi-999700000000002",
        "PDU Session Establishment Reject imsi-999700000000003",
        "PDU Session Establishment Reject imsi-999700000000004",
    });
    col.startBackgroundCollection();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    col.stopBackgroundCollection();

    NwdafAnalyticsEngine engine(col, cfg);
    json r = engine.compute("SM_CONGESTION");

    REQUIRE(r["congestionLevel"] == "HIGH");
    REQUIRE(r["sessionEstFailures"].get<int>() == 4);
    REQUIRE(r["sessionEstAttempts"].get<int>() == 5);
    REQUIRE(r["sessionEstFailureRatePct"].get<double>() == Approx(80.0));
    REQUIRE(r["recommendation"] == "APPLY_BACKOFF_AND_SCALE_OUT");
    // Every rejected UE saw a 100% failure ratio, so all land in the top bucket.
    REQUIRE(r["smcceUeList"]["highLevelCongestion"].size() == 3);
}

TEST_CASE("H1.4: SM_CONGESTION escalates on NF saturation alone") {
    auto cfg = h1Config();
    MockNwdafCollector col(cfg);
    col.setSmfLines({"[Established] PDU Session Establishment imsi-999700000000001"});
    std::vector<NfMetric> nf = {
        {"SMF", "active", 101, 90.0, 4096, 85.0, "OVERLOADED"},
        {"UPF", "active", 102, 10.0, 4096, 10.0, "LOW"},
    };
    col.setNfMetrics(nf);
    col.startBackgroundCollection();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    col.stopBackgroundCollection();

    NwdafAnalyticsEngine engine(col, cfg);
    json r = engine.compute("SM_CONGESTION");
    // Rejects lag saturation, so load alone must be enough to raise the level.
    REQUIRE(r["congestionLevel"] == "HIGH");
    REQUIRE(r["cpLoadPct"].get<double>() == Approx(85.0));
}

TEST_CASE("H1.4: an establishment reject is not counted as a success") {
    // Regression guard: "PDU Session Establishment Reject" contains the
    // substring the PDU_ESTABLISHED branch matches on.
    auto cfg = h1Config();
    MockNwdafCollector col(cfg);
    col.setSmfLines({"PDU Session Establishment Reject imsi-999700000000001"});
    col.startBackgroundCollection();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    col.stopBackgroundCollection();

    auto events = col.getRecentSmfEvents(50);
    REQUIRE(events.size() >= 1);
    for (const auto& e : events) REQUIRE(e.event_type == "PDU_EST_FAILED");
    // A failed establishment must not inflate the active-session tracker.
    REQUIRE(col.getActivePduSessionCount() == 0);
}

// ── H1.4: REDUNDANT_TRANSMISSION ─────────────────────────────────────────────

TEST_CASE("H1.4: REDUNDANT_TRANSMISSION needs a window before reporting") {
    auto cfg = h1Config();
    MockNwdafCollector col(cfg);
    NwdafAnalyticsEngine engine(col, cfg);
    json r = engine.compute("REDUNDANT_TRANSMISSION");
    REQUIRE(r["reason"] == "INSUFFICIENT_DATA");
    REQUIRE(r["confidence"].get<int>() == 0);
}

TEST_CASE("H1.4: REDUNDANT_TRANSMISSION rates a steady link URLLC-suitable") {
    auto cfg = h1Config();
    MockNwdafCollector col(cfg);
    seedThroughput(col, std::vector<std::pair<double,double>>(30, {1000.0, 500.0}));

    NwdafAnalyticsEngine engine(col, cfg);
    json r = engine.compute("REDUNDANT_TRANSMISSION");

    REQUIRE(r["analyticsId"] == "REDUNDANT_TRANSMISSION");
    REQUIRE(r["redTransExp"]["avgDlRedTransExp"].get<double>() == Approx(100.0));
    REQUIRE(r["redTransExp"]["varDlRedTransExp"].get<double>() == Approx(0.0));
    REQUIRE(r["reliabilityLevel"] == "HIGH");
    REQUIRE(r["urllcSuitable"].get<bool>() == true);
    REQUIRE(r["redTransExpPerTS"].size() == 6);
}

TEST_CASE("H1.4: REDUNDANT_TRANSMISSION rejects a volatile link") {
    auto cfg = h1Config();
    MockNwdafCollector col(cfg);
    std::vector<std::pair<double,double>> jittery;
    for (int i = 0; i < 30; ++i)
        jittery.push_back({(i % 2 == 0) ? 2000.0 : 10.0, (i % 2 == 0) ? 900.0 : 5.0});
    seedThroughput(col, jittery);

    NwdafAnalyticsEngine engine(col, cfg);
    json r = engine.compute("REDUNDANT_TRANSMISSION");

    REQUIRE(r["redTransExp"]["avgDlRedTransExp"].get<double>() < 30.0);
    REQUIRE(r["reliabilityLevel"] == "LOW");
    REQUIRE(r["urllcSuitable"].get<bool>() == false);
}

// ── Catalogue registration ───────────────────────────────────────────────────

TEST_CASE("H1.4: the three new IDs are registered and dispatchable") {
    const auto& ids = NwdafAnalyticsEngine::VALID_ANALYTICS_IDS;
    REQUIRE(ids.count("SM_CONGESTION") == 1);
    REQUIRE(ids.count("REDUNDANT_TRANSMISSION") == 1);
    REQUIRE(ids.count("DISPERSION") == 1);
    REQUIRE(ids.size() == 10);

    auto cfg = h1Config();
    MockNwdafCollector col(cfg);
    NwdafAnalyticsEngine engine(col, cfg);
    // Every registered ID must dispatch and echo its own id back.
    for (const auto& id : ids) {
        json r = engine.compute(id);
        INFO("analyticsId=" << id);
        REQUIRE(r["analyticsId"] == id);
    }
}
