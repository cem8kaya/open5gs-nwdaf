# Open5GS NWDAF — C++ Reference Implementation

A production-grade, standalone C++ implementation of the **Network Data Analytics Function (NWDAF)** for Open5GS, compliant with **3GPP TS 23.288 v17** and **TS 29.520 v17**.

## Quick Start

### Prerequisites (Ubuntu 20.04 / 22.04)

```bash
sudo apt-get install -y \
    cmake g++ git pkg-config \
    libyaml-cpp-dev libspdlog-dev \
    libcatch2-dev
```

### Build (without systemd journal, without MongoDB)

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DNWDAF_USE_SD_JOURNAL=OFF \
    -DNWDAF_BUILD_TESTS=ON

cmake --build build --parallel $(nproc)
```

### Run Tests

```bash
cd build && ctest --output-on-failure
```

### Run NWDAF

```bash
./build/open5gs-nwdafd --config config/nwdaf.yaml
```

### Install as systemd service

```bash
sudo cmake --install build
sudo systemctl daemon-reload
sudo systemctl enable --now open5gs-nwdafd
```

---

## REST API (TS 29.520 SBI)

| Method | Path | Description |
|--------|------|-------------|
| GET | `/nwdaf-analytics/v1/health` | Health check |
| GET | `/nwdaf-analytics/v1/analytics?analyticsId=<ID>` | Get analytics |
| POST | `/nwdaf-analytics/v1/subscriptions` | Create subscription |
| GET | `/nwdaf-analytics/v1/subscriptions` | List subscriptions |
| GET | `/nwdaf-analytics/v1/subscriptions/:subId` | Get subscription |
| DELETE | `/nwdaf-analytics/v1/subscriptions/:subId` | Delete subscription |
| POST | `/nwdaf-analytics/v1/train` | Retrain Isolation Forest |

### Example

```bash
curl http://127.0.0.1:7779/nwdaf-analytics/v1/analytics?analyticsId=NF_LOAD
curl http://127.0.0.1:7779/nwdaf-analytics/v1/analytics?analyticsId=ABNORMAL_BEHAVIOUR
```

---

## 3GPP Compliance Table

| Analytics ID | 3GPP TS 23.288 Section | Status |
|---|---|---|
| `NF_LOAD` | §6.5 | Implemented |
| `UE_MOBILITY` | §6.7 | Implemented |
| `UE_COMMUNICATION` | §6.6 | Implemented |
| `ABNORMAL_BEHAVIOUR` | §6.3 | Implemented |
| `QoS_SUSTAINABILITY` | §6.12 | Implemented |
| `SERVICE_EXPERIENCE` | §6.4 | Implemented |
| `NETWORK_PERFORMANCE` | §6.8 | Implemented |

---

## Portability

All deployment-specific values are in `config/nwdaf.yaml`:

| Parameter | Default | Description |
|---|---|---|
| `plmn_mcc` / `plmn_mnc` | 999/70 | Test PLMN |
| `nf_service_names` | AMF→amfd etc. | Open5GS unit suffix map |
| `throughput_interfaces` | ogstun | UPF tunnel interfaces |
| `supi_regex` | `imsi-(\d{15})` | Open5GS v2.7.6 format |
| `sbi_port` | 7779 | Must not conflict with 7777 |
| `nrf_uri` | 127.0.0.1:7777 | NRF for registration |

---

## Architecture

```
NwdafCollector  →  NwdafAnalyticsEngine  →  NwdafServer (cpp-httplib)
    |                     |
 /proc, /sys,        IsolationForest (native C++)
 journald,           EwmaPredictor (native C++)
 MongoDB (opt.)
```

### Known constraints preserved from Python reference

1. **NF name mapping**: Uses explicit `nf_service_names` map — never string-strips service names
2. **AMF regex**: `imsi-` prefix (Open5GS v2.7.6); configurable via `supi_regex`
3. **Throughput**: Reads `/sys/class/net/ogstun/statistics/` — gtp5g bypasses tcpdump/eBPF
4. **Idle baseline guard**: `baseline_stddev_min_kbps` prevents false positives on zero traffic
5. **NETWORK_PERFORMANCE weights**: Fixed 0.6/0.2/0.2 (nfHealth/DL/PDU)

---

## Build Options

| CMake Option | Default | Description |
|---|---|---|
| `NWDAF_USE_SD_JOURNAL` | ON | Use libsystemd journal API |
| `NWDAF_BUILD_TESTS` | ON | Build Catch2 unit + integration tests |

*References: 3GPP TS 23.288 v17.3.0 · TS 29.520 v17.7.0 · TS 29.510 v17.6.0 · TS 28.554 v17.4.0*
