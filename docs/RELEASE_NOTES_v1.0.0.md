# open5gs-nwdaf v1.0.0

**The first open-source, 3GPP Release-17 compliant NWDAF for Open5GS — in modern C++.**

The Network Data Analytics Function is the intelligence layer of the 5G core, and
until now no complete open-source implementation existed for cores like Open5GS.
This is the first public release: a standalone C++17 daemon that runs next to your
Open5GS deployment with **no patches to the core**.

## Highlights

- 📊 **7 Rel-17 analytics IDs** — NF load, UE mobility, UE communication, abnormal
  behaviour, service experience, network performance, QoS sustainability.
- 🤖 **Embedded ML, no Python** — native C++ Isolation Forest (anomaly detection)
  and EWMA (load forecasting); small enough for edge nodes.
- 🛰️ **Open5GS-native** — NRF registration + heartbeat (TS 29.510); collects from
  systemd, journald, `/sys` network stats, and MongoDB.
- 🔐 **Production-grade** — TLS/mTLS, OAuth2, rate limiting, SQLite persistence,
  Prometheus, Grafana, a React dashboard, systemd + Docker, hardened build.
- 🧪 **Tested & CI-green** — 85 Catch2 tests; GitHub Actions matrix across Ubuntu
  22.04 and 20.04.

## Quick start

```bash
git clone https://github.com/cem8kaya/open5gs-nwdaf
cd open5gs-nwdaf
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # add -DNWDAF_USE_TLS=OFF on Ubuntu 20.04
cmake --build build --parallel "$(nproc)"
./build/open5gs-nwdafd --config config/nwdaf.yaml
curl "http://127.0.0.1:7779/nwdaf-analytics/v1/analytics?analyticsId=NF_LOAD"
```

Or via Docker: `docker build -t open5gs-nwdaf . && docker run --rm -p 7779:7779 open5gs-nwdaf`

See the [README](../README.md) for the full setup, configuration reference, and
API. Full changes are in the [CHANGELOG](../CHANGELOG.md).

## Compatibility notes

- **TLS requires OpenSSL ≥ 3.0** (Ubuntu 22.04+). On Ubuntu 20.04 (OpenSSL 1.1.1),
  build with `-DNWDAF_USE_TLS=OFF`.
- **CMake ≥ 3.22** required (Ubuntu 20.04 ships 3.16 — install a newer one).
- First configure needs internet: cpp-httplib, nlohmann/json, yaml-cpp, spdlog,
  Catch2 are fetched via CMake FetchContent.

## We're looking for lab testers 🧪

Run it against your topology (UERANSIM, srsRAN, physical gNBs, multi-UPF) and open
an **Interop Report** issue — successful reports feed a "tested on" matrix. Bug
reports and 3GPP spec-compliance findings are equally welcome.

**Thanks to the [Open5GS](https://open5gs.org) project** for the open-source 5G
core that made this possible.
