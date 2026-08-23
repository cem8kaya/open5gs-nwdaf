#include "ml/mos_estimator.hpp"
#include <algorithm>
#include <cmath>

// ── Throughput impairment ─────────────────────────────────────────────────────
// Weber-Fechner: perceived quality degrades with the logarithm of the shortfall
// against the reference rate. Zero at/above T_REF_KBPS, capped at MAX_IE.
double MosEstimator::throughputImpairment(double kbps) {
    if (kbps >= T_REF_KBPS) return 0.0;
    double t  = std::max(kbps, MIN_KBPS);
    double ie = K_THROUGHPUT * std::log(T_REF_KBPS / t);
    return std::clamp(ie, 0.0, MAX_IE);
}

// ── Packet-loss weighting (G.107 §B.3, random-loss case BurstR = 1) ──────────
double MosEstimator::effectiveImpairment(double ie, double loss_pct) {
    double ppl = std::clamp(loss_pct, 0.0, 100.0);
    if (ppl <= 0.0) return ie;
    return ie + (95.0 - ie) * ppl / (ppl + BPL);
}

// ── Delay impairment (G.107 §B.2, Idd) ───────────────────────────────────────
// Ta is one-way absolute delay; callers supply round-trip, so halve it.
// Below 100 ms the E-model treats delay as perceptually free.
double MosEstimator::delayImpairment(double rtt_ms) {
    double ta = std::max(0.0, rtt_ms) / 2.0;
    if (ta <= 100.0) return 0.0;
    double x = std::log(ta / 100.0) / std::log(2.0);
    double idd = 25.0 * (std::pow(1.0 + std::pow(x, 6.0), 1.0 / 6.0)
                       - 3.0 * std::pow(1.0 + std::pow(x / 3.0, 6.0), 1.0 / 6.0)
                       + 2.0);
    return std::clamp(idd, 0.0, 50.0);
}

// ── R-factor → MOS (G.107 §B.4) ──────────────────────────────────────────────
double MosEstimator::rToMos(double r) {
    if (r <= 0.0)   return 1.0;
    if (r >= 100.0) return 4.5;
    double mos = 1.0 + 0.035 * r + r * (r - 60.0) * (100.0 - r) * 7.0e-6;
    return std::clamp(mos, 1.0, 4.5);
}

std::string MosEstimator::categorize(double mos) {
    if (mos > 4.0) return "EXCELLENT";
    if (mos > 3.5) return "GOOD";
    if (mos > 2.5) return "FAIR";
    return "POOR";
}

// Pre-H1.5 behaviour — retained verbatim as the no-throughput fallback.
double MosEstimator::stepLadderMos(double dl_kbps) {
    if (dl_kbps > 1000) return 4.5;
    if (dl_kbps > 500)  return 4.0;
    if (dl_kbps > 100)  return 3.5;
    if (dl_kbps > 50)   return 3.0;
    return 2.0;
}

MosResult MosEstimator::estimate(const MosInputs& in) {
    MosResult out;

    if (!in.has_throughput) {
        // No usable measurement — degrade to the original step function rather
        // than reporting a modelled value derived from nothing.
        out.method   = "STEP_FALLBACK";
        out.mos      = stepLadderMos(in.dl_kbps);
        out.category = categorize(out.mos);
        return out;
    }

    out.method        = "E_MODEL";
    out.ie_throughput = throughputImpairment(in.dl_kbps);
    out.ie_effective  = in.has_packet_loss
                      ? effectiveImpairment(out.ie_throughput, in.packet_loss_pct)
                      : out.ie_throughput;
    out.id_delay      = in.has_latency ? delayImpairment(in.rtt_ms) : 0.0;

    out.r_factor      = R0 - out.id_delay - out.ie_effective;
    out.mos           = rToMos(out.r_factor);
    out.category      = categorize(out.mos);

    // Per-session view — the aggregate pipe shared across active PDU sessions.
    if (in.active_sessions > 0) {
        out.per_session_available = true;
        out.per_session_kbps      = in.dl_kbps / static_cast<double>(in.active_sessions);

        double ps_ie  = throughputImpairment(out.per_session_kbps);
        double ps_ief = in.has_packet_loss
                      ? effectiveImpairment(ps_ie, in.packet_loss_pct)
                      : ps_ie;
        out.per_session_mos = rToMos(R0 - out.id_delay - ps_ief);
    }

    return out;
}
