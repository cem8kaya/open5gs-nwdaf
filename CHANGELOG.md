# Changelog

All notable changes to this project are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Horizon 1 of [`docs/ENHANCEMENT_PLAN_5G_6G.md`](docs/ENHANCEMENT_PLAN_5G_6G.md),
first quick-wins batch: H1.4 (catalogue completion), H1.5 (service-experience
model) and H1.6 (OpenAPI + conformance).

### Added

**Analytics (TS 23.288)**
- `SM_CONGESTION` (§6.16) — session-management congestion control experience:
  establishment failure ratio, control- and user-plane load, a four-level
  congestion band and the §6.16 `smcceUeList` buckets.
- `REDUNDANT_TRANSMISSION` (§6.12) — per-direction transmission experience,
  variance, a per-time-slot series and a URLLC suitability verdict.
- `DISPERSION` (§6.10) — data-volume dispersion (Gini, coefficient of
  variation, top-decile share) and per-subscriber transaction dispersion
  (Gini, HHI, ranked top talkers).

**Service experience (H1.5)**
- `MosEstimator`: an ITU-T G.107 E-model-style rating replacing the static
  downlink step ladder. Calibrated to the ladder's own anchor points, so
  existing dashboards and alert thresholds keep their meaning while the score
  becomes continuous. `SERVICE_EXPERIENCE` now also returns the R-factor, a
  per-impairment breakdown (throughput / loss / delay) and a per-session MOS.
- Packet-loss and latency inputs are accepted but unset until PFCP usage
  reporting (H1.3) lands; absent signals contribute zero impairment rather
  than a guessed value.

**Contract (H1.6)**
- `docs/openapi/nwdaf-analytics-v1.yaml` — OpenAPI 3.0 description of the SBI.
- `tests/test_openapi_conformance.cpp` — validates live responses from every
  endpoint against that document, so the spec cannot drift from the code. The
  validator is built on yaml-cpp, already a dependency; no new third-party
  library was introduced.

**Data collection**
- SMF parser recognises `PDU_EST_FAILED`, the signal `SM_CONGESTION` needs.

### Changed
- `/health` advertises the engine's registered analytics IDs instead of a
  duplicated literal list, so new IDs reach NF consumers and the NRF profile
  automatically.
- `SERVICE_EXPERIENCE` confidence is 78 when a throughput sample is available
  and 40 when the estimator falls back to the step ladder, replacing the
  previous fixed 75.

### Fixed
- An SMF line reading "PDU Session Establishment Reject" was counted as a
  successful establishment, because the reject branch did not exist and the
  success branch matched on a substring of it. Rejects are now matched first.
- `MockNwdafCollector::setNfMetrics()` stored metrics that nothing ever read,
  leaving every NF-load-dependent path untested. `collectNfLoad()` is now
  virtual and the mock serves the injected metrics.

### Test coverage
- 118 test cases, up from 85.

## [1.0.0] — 2026-07-09

First public release: a standalone, 3GPP Release-17 compliant **NWDAF** for the
Open5GS ecosystem, written in modern C++17.

### Added

**Analytics (TS 23.288 Rel-17)**
- Seven analytics IDs served over an Nnwdaf-style SBI: `NF_LOAD` (§6.5),
  `UE_MOBILITY` (§6.7.2), `UE_COMMUNICATION` (§6.7.3), `ABNORMAL_BEHAVIOUR`
  (§6.7.5), `SERVICE_EXPERIENCE` (§6.4), `NETWORK_PERFORMANCE` (§6.6),
  `QoS_SUSTAINABILITY` (§6.9).

**Machine learning (native C++, no Python runtime)**
- Isolation Forest for anomaly detection — configurable contamination and seed,
  a 120-sample retrain quality gate, an idle-baseline guard, and atomic
  write-then-rename model persistence.
- EWMA predictor for NF load forecasting.
- On-demand retraining via `POST /nwdaf-analytics/v1/train`.

**Service-Based Interface (TS 29.520)**
- `Nnwdaf_AnalyticsInfo` (GET + spec-compliant POST) and
  `Nnwdaf_EventsSubscription` (create/list/get/delete) with background push
  notification delivery.
- Health (`/health`), readiness (`/ready`), and Prometheus (`/metrics`) endpoints.

**Open5GS integration**
- NRF registration and heartbeat per TS 29.510 §5.3.2.4.
- Data collection from real Open5GS surfaces with no core patches: systemd unit
  states (NF health), AMF/SMF journald with a configurable SUPI regex,
  `/sys/class/net/<iface>/statistics` throughput (gtp5g-safe), and optional
  MongoDB subscriber count.

**Security & operations**
- TLS/mTLS on the SBI per TS 33.501 §13.3 (requires OpenSSL ≥ 3.0).
- OAuth 2.0 bearer-token validation.
- Token-bucket rate limiting (per-IP and global).
- SQLite-backed throughput history and subscription persistence across restarts.
- Hardened build flags: `-Wall -Wextra -Werror`, `-D_FORTIFY_SOURCE=2`,
  `-fstack-protector-strong`, PIE, full RELRO.

**Tooling & deployment**
- React + Recharts "NWDAF Intelligence" web dashboard and Grafana dashboard JSON.
- Reproducible multi-stage Docker build (Ubuntu 22.04) and a systemd unit.
- 85 Catch2 test cases (unit + integration) with a mock Open5GS environment.
- GitHub Actions CI: Ubuntu 22.04 and 20.04 build matrix (full / minimal
  profiles), `ctest`, and a Docker image build.

### Known limitations

- **TLS requires OpenSSL ≥ 3.0** (Ubuntu 22.04+). On Ubuntu 20.04 (OpenSSL 1.1.1),
  build with `-DNWDAF_USE_TLS=OFF`; all other features are unaffected.
- Not yet implemented: MTLF/AnLF split, `SLICE_LOAD_LEVEL`, `DN_PERFORMANCE`,
  and slice-level analytics. See the roadmap in the README.

[1.0.0]: https://github.com/cem8kaya/open5gs-nwdaf/releases/tag/v1.0.0
