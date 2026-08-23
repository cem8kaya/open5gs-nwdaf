#pragma once
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// H1.5 — Service-experience (MOS) estimator.
//
// Replaces the static downlink-throughput step ladder that SERVICE_EXPERIENCE
// used to apply (dl>1000 → 4.5, dl>500 → 4.0, …) with a continuous
// ITU-T G.107 "E-model"-style transmission-rating computation:
//
//     R = R0 - Id(delay) - Ie_eff(throughput, loss)
//     MOS = f(R)                                    [G.107 §B.4]
//
// The throughput term follows the Weber-Fechner law (quality scales with the
// logarithm of the stimulus) and is calibrated so the estimator reproduces the
// old ladder's anchor points to within ~0.15 MOS — 50 kbps → 3.0,
// 1000 kbps → 4.4 — so existing dashboards and alert thresholds stay valid
// while the output becomes continuous and differentiable.
//
// Packet loss and latency are optional inputs: they are unavailable from the
// current /sys/class/net data path and become populated once PFCP usage
// reporting lands (H1.3). Absent inputs contribute zero impairment rather than
// a guessed value, following the repository's graceful-degradation convention.
// When throughput itself is unavailable the estimator falls back to the
// original step ladder so the analytics never returns a bare error.
// ─────────────────────────────────────────────────────────────────────────────

struct MosInputs {
    bool   has_throughput  = false;
    double dl_kbps         = 0.0;
    double ul_kbps         = 0.0;

    // Optional — populated by the PFCP usage-report source (H1.3).
    bool   has_packet_loss = false;
    double packet_loss_pct = 0.0;   // percent, 0..100
    bool   has_latency     = false;
    double rtt_ms          = 0.0;   // round-trip; halved to one-way for G.107

    // Contention context — active PDU sessions sharing the measured pipe.
    int    active_sessions = 0;
};

struct MosResult {
    double      mos            = 1.0;
    double      r_factor       = 0.0;
    std::string category       = "POOR";
    std::string method         = "STEP_FALLBACK";  // "E_MODEL" | "STEP_FALLBACK"

    // Impairment breakdown — per-factor attribution for operator explainability
    // (and the seed of the H2.7 explainability work).
    double ie_throughput       = 0.0;   // throughput-derived equipment impairment
    double ie_effective        = 0.0;   // above, after packet-loss weighting
    double id_delay            = 0.0;   // delay impairment

    // Per-session view: the aggregate pipe divided across active PDU sessions.
    // This is the closer analogue of per-subscriber experience; the headline
    // `mos` stays aggregate so it remains comparable with the previous release.
    bool        per_session_available = false;
    double      per_session_kbps      = 0.0;
    double      per_session_mos       = 0.0;
};

class MosEstimator {
public:
    // G.107 default transmission rating for a clean reference connection.
    static constexpr double R0            = 93.2;
    // Throughput at or above which no throughput impairment is applied.
    static constexpr double T_REF_KBPS    = 1000.0;
    // Weber-Fechner slope, calibrated so 50 kbps yields MOS 3.0 (see header).
    static constexpr double K_THROUGHPUT  = 11.72;
    // G.107 packet-loss robustness factor; 10 is typical for elastic data.
    static constexpr double BPL           = 10.0;
    // Floor applied before taking a logarithm, and the impairment ceiling.
    // The pair is chosen so a fully idle link lands on exactly R = 0 → MOS 1.0
    // (unusable), and the curve is strictly monotone everywhere above the
    // floor: K_THROUGHPUT * ln(T_REF_KBPS / MIN_KBPS) == MAX_IE == R0.
    static constexpr double MIN_KBPS      = 0.35;
    static constexpr double MAX_IE        = R0;

    static MosResult estimate(const MosInputs& in);

    // Components are individually public so they can be unit-tested and reused.
    static double      throughputImpairment(double kbps);
    static double      effectiveImpairment(double ie, double loss_pct);
    static double      delayImpairment(double rtt_ms);
    static double      rToMos(double r);
    static std::string categorize(double mos);

    // The pre-H1.5 behaviour, retained as the no-throughput fallback.
    static double      stepLadderMos(double dl_kbps);
};
