# Changelog

All notable changes to this project are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
