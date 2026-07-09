# Open5GS NWDAF — C++ Implementation
## Setup · Build · Test Guide
### Ubuntu 20.04 LTS + VS Code

---

> **Scope of this guide**: Building and testing the NWDAF on your **local Ubuntu 20
> development machine**. No Open5GS installation is required — all Open5GS
> interfaces (journald, /proc, /sys, MongoDB) are mocked for local development.
> Deployment to a live Open5GS node is covered in the final section.

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [Install Build Toolchain](#2-install-build-toolchain)
3. [Install Runtime Dependencies](#3-install-runtime-dependencies)
4. [Install VS Code and Extensions](#4-install-vs-code-and-extensions)
5. [Clone and Initialise the Project](#5-clone-and-initialise-the-project)
6. [Configure the Project](#6-configure-the-project)
7. [Build](#7-build)
8. [Run Unit & Integration Tests](#8-run-unit--integration-tests)
9. [REST API Reference](#9-rest-api-reference)
10. [Manual API Smoke Test](#10-manual-api-smoke-test)
11. [VS Code Debugging](#11-vs-code-debugging)
12. [Deploy to Open5GS Node](#12-deploy-to-open5gs-node)
13. [Troubleshooting](#13-troubleshooting)

---

## 1. Prerequisites

### System requirements

| Item | Requirement |
|------|-------------|
| OS | Ubuntu 20.04 LTS (focal) |
| CPU | Any x86-64 |
| RAM | ≥ 4 GB (8 GB recommended) |
| Disk | ≥ 5 GB free |
| Internet | Required for FetchContent dependencies |

### Check your Ubuntu version

```bash
lsb_release -a
# Expected: Ubuntu 20.04.x LTS
```

---

## 2. Install Build Toolchain

```bash
# Update package lists
sudo apt update

# Core build tools
sudo apt install -y \
    build-essential \
    gcc-10 \
    g++-10 \
    cmake \
    ninja-build \
    git \
    curl \
    wget \
    pkg-config \
    gdb

# Set gcc-10 / g++-10 as default (Ubuntu 20 ships gcc-9 by default)
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-10 100
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-10 100

# Verify
g++ --version   # Should print: g++ (Ubuntu 10.x.x) 10.x.x
cmake --version # Should print: cmake version 3.x.x
```

> **CMake version note**: Ubuntu 20's `apt` ships CMake 3.16, but the project
> requires 3.22. Install a newer version:

```bash
# Remove apt cmake
sudo apt remove -y cmake

# Install via pip (simplest method on Ubuntu 20)
pip3 install cmake --upgrade

# Or via official Kitware APT repository:
wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null \
    | gpg --dearmor - \
    | sudo tee /usr/share/keyrings/kitware-archive-keyring.gpg > /dev/null

echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] \
    https://apt.kitware.com/ubuntu/ focal main' \
    | sudo tee /etc/apt/sources.list.d/kitware.list

sudo apt update && sudo apt install -y cmake

# Verify
cmake --version  # Should print: 3.28.x or higher
```

---

## 3. Install Runtime Dependencies

### 3.1 yaml-cpp

```bash
sudo apt install -y libyaml-cpp-dev

# Verify
pkg-config --modversion yaml-cpp
```

### 3.2 spdlog

```bash
sudo apt install -y libspdlog-dev

# Verify
pkg-config --modversion spdlog 2>/dev/null || echo "spdlog installed (no pkg-config entry on Ubuntu 20)"
```

### 3.3 libsystemd-dev (for sd-journal API — optional for local dev)

```bash
sudo apt install -y libsystemd-dev

# Verify
pkg-config --modversion libsystemd
```

> **Local dev note**: When building on Ubuntu 20 without Open5GS running,
> set the CMake flag `-DNWDAF_USE_SD_JOURNAL=OFF`. The build will fall back
> to the `journalctl` subprocess path, which returns empty events on a machine
> without Open5GS — which is exactly what the mock tests expect.

### 3.3b TLS / OpenSSL note (important for Ubuntu 20.04)

The SBI TLS/mTLS support (`NWDAF_USE_TLS`, **ON by default**) is built on
cpp-httplib, which **requires OpenSSL ≥ 3.0**. Ubuntu 20.04 ships OpenSSL
**1.1.1**, so a default build there fails to compile `nwdaf_server.cpp` with
`#error Sorry, OpenSSL versions prior to 3.0.0 are not supported`.

- **Ubuntu 20.04:** build with **`-DNWDAF_USE_TLS=OFF`** (all other features work).
- **Ubuntu 22.04+ (OpenSSL 3.0):** TLS builds normally; install `libssl-dev`.

### 3.4 MongoDB C++ Driver (optional)

MongoDB is used only for subscriber count (queries the `subscribers` collection
in the `open5gs` DB via UDR/UDM). It is optional — CMake detects mongocxx via
`find_package(mongocxx QUIET)`. When found, it defines `NWDAF_HAS_MONGODB` and
links `mongo::mongocxx_shared`. When absent, `getSubscriberCount()` always
returns `0` and the build still succeeds.

```bash
# Install libmongoc and libmongocxx
sudo apt install -y libmongoc-dev libbson-dev

# The mongocxx driver must be built from source on Ubuntu 20:
cd /tmp
git clone https://github.com/mongodb/mongo-cxx-driver.git --branch r3.10.1 --depth=1
cd mongo-cxx-driver/build
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_INSTALL_PREFIX=/usr/local \
         -DBUILD_SHARED_LIBS=ON
sudo cmake --build . --target install --parallel 4
sudo ldconfig

# Verify
pkg-config --modversion libmongocxx
```

> **Skip MongoDB for local dev**: If you just want to build and test quickly,
> skip this step. CMake will print a warning and proceed without MongoDB support.

### 3.5 Catch2 (test framework)

```bash
cd /tmp
git clone https://github.com/catchorg/Catch2.git --branch v3.5.2 --depth=1
cd Catch2
cmake -S . -B build -DBUILD_TESTING=OFF
sudo cmake --build build --target install --parallel 4
```

---

## 4. Install VS Code and Extensions

### Install VS Code

```bash
# Download and install
wget -qO /tmp/code.deb \
    "https://code.visualstudio.com/sha/download?build=stable&os=linux-deb-x64"
sudo dpkg -i /tmp/code.deb
sudo apt install -f   # Fix any dependency issues

# Launch
code
```

### Install required extensions

Open VS Code and install these extensions (Ctrl+Shift+X):

| Extension ID | Name | Purpose |
|---|---|---|
| `ms-vscode.cpptools` | C/C++ | IntelliSense, debugging |
| `ms-vscode.cmake-tools` | CMake Tools | CMake integration, build tasks |
| `ms-vscode.cpptools-extension-pack` | C/C++ Extension Pack | Bundled tools |
| `vadimcn.vscode-lldb` | CodeLLDB | Alternative debugger (optional) |
| `twxs.cmake` | CMake | CMake syntax highlighting |

Or install from terminal:

```bash
code --install-extension ms-vscode.cpptools
code --install-extension ms-vscode.cmake-tools
code --install-extension twxs.cmake
```

---

## 5. Clone and Initialise the Project

```bash
# Clone the repository
git clone https://github.com/cem8kaya/5g-ai-lab.git
cd 5g-ai-lab/nwdaf-cpp

# Or, if the C++ NWDAF is in its own repo:
git clone https://github.com/cem8kaya/open5gs-nwdaf-cpp.git
cd open5gs-nwdaf-cpp
```

Verify the directory structure:

```bash
tree -L 2
# Expected output:
# .
# ├── CMakeLists.txt
# ├── config/
# │   └── nwdaf.yaml
# ├── include/
# │   ├── ml/
# │   ├── nwdaf_analytics.hpp
# │   ├── nwdaf_collector.hpp
# │   ├── nwdaf_config.hpp
# │   ├── nwdaf_server.hpp
# │   └── nwdaf_subscription.hpp
# ├── src/
# │   ├── main.cpp
# │   ├── ml/
# │   └── ...
# ├── systemd/
# │   └── open5gs-nwdafd.service
# └── tests/
```

Open in VS Code:

```bash
code .
```

---

## 6. Configure the Project

### 6.1 Edit `config/nwdaf.yaml` for local development

The shipped `config/nwdaf.yaml` is production-oriented (logs to
`/var/log/open5gs/nwdaf.log`, registers with NRF on startup). For local dev,
override the following keys:

```yaml
nwdaf:
  sbi_bind_address: "127.0.0.1"
  sbi_port: 7779

  # These won't match anything on your dev machine — that's fine.
  # Collectors will return empty/zero data; mock tests inject their own data.
  mongodb_uri: "mongodb://127.0.0.1:27017"
  nrf_register_on_startup: false  # Disable for local dev (no NRF available)

  # ML knobs
  model_dir: "/tmp/nwdaf_models"       # Isolation Forest JSON is cached here
  anomaly_contamination: 0.10          # Expected anomaly fraction
  anomaly_min_samples: 10              # Below this → INSUFFICIENT_DATA
  baseline_stddev_min_kbps: 0.5        # Below this → BASELINE_TOO_LOW
  ewma_alpha: 0.3                      # EWMA smoothing (0..1)

  log_level: "debug"
  log_file: ""    # Empty = console-only; spdlog silently skips file sink on failure
```

The full config schema is defined in `include/nwdaf_config.hpp` — every key
listed in `config/nwdaf.yaml` is consumed. Note that
`throughput_history_size: 360` at a 10 s `collection_interval_seconds` retains
**one hour** of throughput samples used by `ABNORMAL_BEHAVIOUR` and
`QoS_SUSTAINABILITY`.

### 6.2 CMake configuration

The project exposes two options (see `CMakeLists.txt`). **Both default to `ON`**
— you must explicitly pass `-DNWDAF_USE_SD_JOURNAL=OFF` on a dev box that
doesn't have a running Open5GS journald feed, otherwise the
`libsystemd` dev headers become a hard requirement and (at runtime)
`sd_journal_open` will just return empty lists.

| Flag | Default | Value for local dev | Value for Open5GS node |
|---|---|---|---|
| `NWDAF_USE_SD_JOURNAL` | `ON` | `OFF` (use `journalctl` subprocess) | `ON` (use sd-journal API + `sd_notify`) |
| `NWDAF_BUILD_TESTS`    | `ON` | `ON` | `ON` or `OFF` |
| `CMAKE_BUILD_TYPE`     | (none) | `Debug` | `Release` |

MongoDB support is **not** controlled by an option — CMake probes for
`mongocxx` via `find_package(mongocxx QUIET)` and, if present, defines the
`NWDAF_HAS_MONGODB` compile macro used in `nwdaf_collector.cpp`.

---

## 7. Build

### Method A: VS Code CMake Tools (recommended)

1. Open VS Code in the project directory
2. Press `Ctrl+Shift+P` → `CMake: Configure`
3. Select kit: **GCC 10.x.x** (or the highest available)
4. VS Code will run cmake configure automatically
5. Press `Ctrl+Shift+P` → `CMake: Build` (or press `F7`)

Watch the output panel — a successful build ends with:

```
[100%] Linking CXX executable open5gs-nwdafd
[100%] Built target open5gs-nwdafd
```

### Method B: Terminal (manual)

```bash
# Create build directory
mkdir -p build && cd build

# Configure — local dev (no sd-journal, no MongoDB required)
# NOTE: -DNWDAF_USE_TLS=OFF is required on Ubuntu 20.04 — its OpenSSL is 1.1.1,
# but cpp-httplib's TLS support needs OpenSSL >= 3.0 (Ubuntu 22.04+).
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DNWDAF_USE_SD_JOURNAL=OFF \
    -DNWDAF_USE_TLS=OFF \
    -DNWDAF_BUILD_TESTS=ON \
    -G Ninja

# Build
ninja -j4

# Or with make:
# cmake .. -DCMAKE_BUILD_TYPE=Debug -DNWDAF_USE_SD_JOURNAL=OFF -DNWDAF_USE_TLS=OFF
# make -j4

cd ..
```

Expected output:

```
-- CMake version: 3.28.x
-- MongoDB C++ driver not found — subscriber count disabled (returns 0)
-- Configuring done
-- Build files have been written to: /path/to/build

[1/42] Building CXX object src/ml/CMakeFiles/...
...
[42/42] Linking CXX executable open5gs-nwdafd
```

### Verify the binary

```bash
./build/open5gs-nwdafd --help
# Expected: Usage: open5gs-nwdafd [--config <path>]
```

---

## 8. Run Unit & Integration Tests

All unit tests and integration tests compile into a **single** Catch2 binary —
`build/tests/nwdaf_tests`. `catch_discover_tests(nwdaf_tests)` registers every
`TEST_CASE` as an individual `ctest` entry, so `ctest` will list many more
than three files' worth of names.

The integration test fixture boots the NWDAF SBI server on port **17779**
(not 7779) inside the test process and issues real HTTP calls against it.

### Method A: VS Code CMake Tools

Press `Ctrl+Shift+P` → `CMake: Run Tests`

### Method B: Terminal

```bash
cd build
ctest --output-on-failure --verbose
```

Or run the Catch2 binary directly (much faster than ctest for dev):

```bash
./build/tests/nwdaf_tests                      # Run everything
./build/tests/nwdaf_tests --list-tests         # List all test cases
./build/tests/nwdaf_tests "IsolationForest*"   # Single test case
./build/tests/nwdaf_tests -s                   # Show successful assertions too
```

### Expected output

```
===============================================================================
All tests passed (N assertions in 24 test cases)
```

The suite currently covers:

| File | Test cases | Focus |
|---|---|---|
| `tests/test_collector.cpp` | 5 | Mock collector: AMF/SMF parsing, subscriber count, background thread, throughput sample fields |
| `tests/test_analytics.cpp` | 9 | Seven analytics IDs + `IsolationForest` outlier scoring + `EwmaPredictor` lag |
| `tests/test_server_integration.cpp` | 10 | HTTP: `/health`, `/analytics`, `/subscriptions` CRUD, all 7 analytics IDs, `/train` |

### Filter tests by prefix

ctest matches against the Catch2 test-case name, so use the human-readable
prefixes rather than file names:

```bash
cd build
ctest -R "Integration:"              --output-on-failure   # server integration only
ctest -R "IsolationForest"           --output-on-failure   # anomaly model only
ctest -R "EwmaPredictor"             --output-on-failure   # EWMA only
ctest -R "MockCollector:"            --output-on-failure   # collector mocks only
ctest -R "ABNORMAL_BEHAVIOUR"        --output-on-failure   # abnormal behaviour scenarios
```

---

### What the integration tests verify

| # | Catch2 case | Asserts |
|---|---|---|
| 1 | `Integration: GET /health returns 200 UP` | `status == "UP"` |
| 2 | `Integration: GET /analytics?analyticsId=NF_LOAD returns 200` | HTTP 200, `analData.nfLoadLevelList` present |
| 3 | `Integration: GET /analytics without param returns 400` | HTTP 400 |
| 4 | `Integration: GET /analytics?analyticsId=INVALID returns 422` | HTTP 422 |
| 5 | `Integration: POST /subscriptions ...` | HTTP 201, body has `subId` |
| 6 | `Integration: GET /subscriptions/<subId> returns 200` | HTTP 200, `subId` echoed |
| 7 | `Integration: DELETE /subscriptions/<subId> returns 204` | HTTP 204 |
| 8 | `Integration: GET /subscriptions/<subId> after delete returns 404` | HTTP 404 |
| 9 | `Integration: All 7 analytics IDs return 200 with correct analyticsId` | For every ID: HTTP 200, `analyticsId` field matches |
| 10 | `Integration: POST /train returns 200` | HTTP 200 |

---

## 9. REST API Reference

All endpoints are rooted at `/nwdaf-analytics/v1` and speak JSON. The SBI
server is `cpp-httplib` (HTTP/1.1 plaintext — no TLS; if you need mTLS, place
it behind a reverse proxy).

| Method | Path | Purpose |
|---|---|---|
| `GET`    | `/health` | Liveness + NF identity |
| `GET`    | `/analytics?analyticsId=<ID>&supi=&startTs=&endTs=` | Nnwdaf_AnalyticsInfo (TS 29.520 §5.2) |
| `POST`   | `/subscriptions` | Create Nnwdaf_EventsSubscription (§5.3) |
| `GET`    | `/subscriptions` | List all active subscriptions |
| `GET`    | `/subscriptions/{subId}` | Fetch one |
| `DELETE` | `/subscriptions/{subId}` | Terminate one |
| `POST`   | `/train` | Force-retrain the Isolation Forest on current throughput history |

### Analytics response envelope

`GET /analytics` wraps the per-analytics payload in a 3GPP-style envelope:

```json
{
  "analyticsId":  "NF_LOAD",
  "requestTime":  "2026-04-21T09:12:03Z",
  "timeStampGen": "2026-04-21T09:12:03Z",
  "validity":     60,
  "confidence":   90,
  "analData":     { "...": "per-analytics payload below" }
}
```

The seven payload shapes under `analData` are defined in
`src/nwdaf_analytics.cpp`:

| analyticsId | Key fields in `analData` |
|---|---|
| `NF_LOAD` | `nfLoadLevelList[]`, `overloadedNfs[]`, `recommendation` (`STABLE` / `SCALE_OUT`) |
| `UE_MOBILITY` | `registrationCount`, `deregistrationCount`, `handoverCount`, `authFailureCount`, `mobilityPattern` (`LOW`/`HIGH`) |
| `UE_COMMUNICATION` | `pduSessionEstCount`, `pduSessionRelCount`, `activePduSessions`, `totalSubscribers`, `currentDlKbps`, `currentUlKbps` |
| `ABNORMAL_BEHAVIOUR` | `anomalyDetected`, `anomalyType`, `anomalyPct`, `anomalyIndices`, `avgAnomalyScore`, `baselineDlStd`, `reason` (when gated) |
| `QoS_SUSTAINABILITY` | `currentDlKbps`, `predictedDlKbps`, `dlTrend`, `ulTrend`, `violationRisk` (`LOW`/`MEDIUM`/`HIGH`) |
| `SERVICE_EXPERIENCE` | `mosScore`, `mosCategory` (`POOR`..`EXCELLENT`), `activeSessionRatio` |
| `NETWORK_PERFORMANCE` | `overallScore`, `scoreLabel`, `components.{nfHealthScore,dlScore,pduScore}` |

### Subscription request body

`POST /subscriptions` accepts:

```json
{
  "analyticsId":  "NF_LOAD",            // REQUIRED
  "notifUri":     "http://.../callback",// REQUIRED
  "notifId":      "my-corr-id",         // optional
  "repPeriod":    60,                   // optional (seconds, default 60)
  "maxReportNbr": 0                     // optional (0 = unlimited)
}
```

Only `analyticsId` and `notifUri` are validated; missing either returns
HTTP 400. The response is HTTP 201 with body:

```json
{
  "subId":       "sub-<16 hex>",
  "analyticsId": "NF_LOAD",
  "notifUri":    "http://.../callback",
  "status":      "ACTIVE",
  "createdAt":   "2026-04-21T09:12:03Z"
}
```

> **Implementation note**: The subscription store is currently in-process
> only (no outbound push delivery). That matches the 3GPP compliance table
> in the Quick Reference: store ✅, push ⚠️.

---

## 10. Manual API Smoke Test

Start the NWDAF in a terminal:

```bash
./build/open5gs-nwdafd --config config/nwdaf.yaml
```

You should see (exact strings from `src/main.cpp` / `src/nwdaf_server.cpp`):

```
[2026-04-21 12:00:00.123] [nwdaf] [info] Open5GS NWDAF starting (instance: <uuid>)
[2026-04-21 12:00:00.124] [nwdaf] [warn] NRF registration failed (status -1)   # only if nrf_register_on_startup=true and NRF is down
[2026-04-21 12:00:00.125] [nwdaf] [info] NWDAF SBI server starting on 127.0.0.1:7779
[2026-04-21 12:00:00.125] [nwdaf] [info] NWDAF ready on 127.0.0.1:7779
```

When built with `-DNWDAF_USE_SD_JOURNAL=ON` and launched under systemd, the
binary additionally calls `sd_notify(READY=1)` (Type=notify) and `STOPPING=1`
on shutdown — this is what the packaged `systemd/open5gs-nwdafd.service`
relies on.

Open a **second terminal** and run these smoke tests:

### Health check

```bash
curl -s http://localhost:7779/nwdaf-analytics/v1/health | python3 -m json.tool
```

Expected (from `handleHealth()` in `src/nwdaf_server.cpp:73`):
```json
{
    "status":       "UP",
    "nfType":       "NWDAF",
    "nfInstanceId": "<uuid>",
    "ts":           "2026-04-21T09:12:03Z"
}
```

### Query all 7 Analytics IDs

Note the mixed-case `QoS_SUSTAINABILITY` — the validator in
`NwdafAnalyticsEngine::VALID_ANALYTICS_IDS` is case-sensitive, so
`QOS_SUSTAINABILITY` will return **HTTP 422**.

```bash
for ID in NF_LOAD UE_MOBILITY UE_COMMUNICATION ABNORMAL_BEHAVIOUR \
          QoS_SUSTAINABILITY SERVICE_EXPERIENCE NETWORK_PERFORMANCE; do
    echo "=== $ID ==="
    curl -s "http://localhost:7779/nwdaf-analytics/v1/analytics?analyticsId=$ID" \
        | python3 -m json.tool
    echo ""
done
```

Optional filters supported by `GET /analytics`:

| Query param | Used by | Behaviour |
|---|---|---|
| `supi=imsi-<15 digits>` | `UE_MOBILITY` | Filters AMF events to one subscriber; other IDs ignore it |
| `startTs`, `endTs` | Reserved | Accepted but not yet applied to the window — present for 3GPP conformance |

### Subscription lifecycle

```bash
# Create subscription (only analyticsId + notifUri are required)
SUB=$(curl -s -X POST http://localhost:7779/nwdaf-analytics/v1/subscriptions \
    -H "Content-Type: application/json" \
    -d '{"analyticsId":"NF_LOAD","notifUri":"http://localhost:9000/notify","repPeriod":30}')
echo $SUB | python3 -m json.tool

# Extract subId
SUB_ID=$(echo $SUB | python3 -c "import sys,json; print(json.load(sys.stdin)['subId'])")

# List all subscriptions
curl -s http://localhost:7779/nwdaf-analytics/v1/subscriptions | python3 -m json.tool

# Get one subscription
curl -s http://localhost:7779/nwdaf-analytics/v1/subscriptions/$SUB_ID | python3 -m json.tool

# Delete subscription → 204 on success, 404 on second attempt
curl -s -o /dev/null -w "Delete 1: HTTP %{http_code}\n" \
    -X DELETE http://localhost:7779/nwdaf-analytics/v1/subscriptions/$SUB_ID
curl -s -o /dev/null -w "Delete 2: HTTP %{http_code}\n" \
    -X DELETE http://localhost:7779/nwdaf-analytics/v1/subscriptions/$SUB_ID
```

### Error handling

```bash
# Missing analyticsId → 400
curl -s http://localhost:7779/nwdaf-analytics/v1/analytics | python3 -m json.tool

# Invalid analyticsId → 422
curl -s "http://localhost:7779/nwdaf-analytics/v1/analytics?analyticsId=FAKE" | python3 -m json.tool
```

### Trigger ML model training

```bash
# Wait at least 30 seconds after startup for throughput history to accumulate
sleep 30
curl -s -X POST http://localhost:7779/nwdaf-analytics/v1/train | python3 -m json.tool
```

---

## 11. VS Code Debugging

### Debug the main NWDAF binary

1. Open VS Code in the project root
2. Set a breakpoint — e.g., in `src/nwdaf_analytics.cpp` in the
   `NwdafAnalyticsEngine::nfLoad()` function (click the gutter to the left of
   the line number)
3. Press `F5` (or go to Run → Start Debugging)
4. Select the **"Debug NWDAF"** configuration
5. VS Code will build, launch the binary, and stop at your breakpoint
6. In a second terminal: `curl http://localhost:7779/nwdaf-analytics/v1/analytics?analyticsId=NF_LOAD`
7. The debugger will stop at your breakpoint — inspect variables in the
   sidebar, step through code with `F10` (step over) / `F11` (step into)

### Debug the test suite

1. Set a breakpoint in `tests/test_analytics.cpp`
2. Press `F5` and select **"Debug Tests"**
3. The test binary runs under gdb — execution stops at your breakpoint

### Useful VS Code shortcuts

| Shortcut | Action |
|---|---|
| `F5` | Start debugging |
| `F7` | Build |
| `Ctrl+Shift+P → CMake: Configure` | Re-run CMake configure |
| `F9` | Toggle breakpoint |
| `F10` | Step over |
| `F11` | Step into |
| `Shift+F11` | Step out |
| `Ctrl+Shift+~` | Open integrated terminal |

---

## 12. Deploy to Open5GS Node

Once you are satisfied with local testing, deploy to a machine running Open5GS.

### 12.1 Build for production

```bash
mkdir -p build-release && cd build-release

cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DNWDAF_USE_SD_JOURNAL=ON \
    -DNWDAF_BUILD_TESTS=OFF

make -j$(nproc)
```

### 12.2 Install

```bash
# Copy binary
sudo cp build-release/open5gs-nwdafd /usr/local/bin/
sudo chmod 755 /usr/local/bin/open5gs-nwdafd

# Install config
sudo mkdir -p /etc/open5gs
sudo cp config/nwdaf.yaml /etc/open5gs/nwdaf.yaml

# Create model and log directories
sudo mkdir -p /opt/nwdaf/models
sudo mkdir -p /var/log/open5gs
sudo chown -R $(whoami):$(whoami) /opt/nwdaf

# Install systemd service
sudo cp systemd/open5gs-nwdafd.service /etc/systemd/system/
sudo systemctl daemon-reload
```

> **Type=notify**: The unit file uses `Type=notify` with `WatchdogSec=30` and
> expects the daemon to call `sd_notify(READY=1)`. This path is only compiled
> in when `-DNWDAF_USE_SD_JOURNAL=ON` (see `src/main.cpp:117`). If you build
> without sd-journal support and drop the binary behind this unit, systemd
> will time out the startup — either flip the build option back on or edit
> the unit to `Type=simple` and remove `WatchdogSec`.

### 12.3 Adapt `nwdaf.yaml` for your Open5GS deployment

Edit `/etc/open5gs/nwdaf.yaml`:

```yaml
nwdaf:
  plmn_mcc: "999"      # ← your PLMN
  plmn_mnc: "70"

  # Adjust to your kernel interface names
  throughput_interfaces:
    - "ogstun"
    - "ens4"        # or eth0, ens3, etc.

  # Adjust MongoDB if using a non-default setup
  mongodb_uri: "mongodb://127.0.0.1:27017"
  mongodb_db:  "open5gs"

  nrf_uri: "http://127.0.0.1:7777"
  nrf_register_on_startup: true

  log_level: "info"
  log_file: "/var/log/open5gs/nwdaf.log"
```

### 12.4 Start and enable the service

```bash
sudo systemctl enable open5gs-nwdafd
sudo systemctl start  open5gs-nwdafd

# Verify
sudo systemctl status open5gs-nwdafd

# Health check
curl http://localhost:7779/nwdaf-analytics/v1/health
```

### 12.5 View logs

```bash
# Journald (recommended)
sudo journalctl -u open5gs-nwdafd -f

# Log file
tail -f /var/log/open5gs/nwdaf.log
```

### 12.6 NRF registration

The NWDAF registers itself with the NRF on startup (if `nrf_register_on_startup: true`).
Verify the registration:

```bash
curl http://127.0.0.1:7777/nnrf-nfm/v1/nf-instances | python3 -m json.tool | grep -A5 NWDAF
```

---

## 13. Troubleshooting

### Build fails: `Could not find yaml-cpp`

```bash
sudo apt install -y libyaml-cpp-dev
# If still not found, specify path explicitly:
cmake .. -Dyaml-cpp_DIR=/usr/lib/x86_64-linux-gnu/cmake/yaml-cpp
```

### Build fails: `Could not find spdlog`

```bash
sudo apt install -y libspdlog-dev
# Ubuntu 20 spdlog version may be old; build from source if needed:
cd /tmp && git clone https://github.com/gabime/spdlog.git --branch v1.13.0 --depth=1
cd spdlog && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
sudo cmake --build build --target install
```

### Build fails: `FetchContent cmake error` (no internet)

Manually download dependencies:

```bash
# cpp-httplib
cd /tmp && git clone https://github.com/yhirose/cpp-httplib.git --branch v0.15.3 --depth=1
# Then in CMakeLists.txt, replace FetchContent with:
# add_subdirectory(/tmp/cpp-httplib)

# nlohmann/json
wget https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz -O /tmp/json.tar.xz
```

### CMake version too old

```bash
# Install from pip
pip3 install --upgrade cmake
hash -r   # Refresh shell PATH
cmake --version
```

### Port 7779 already in use

```bash
# Find what is using port 7779
sudo ss -tlnp | grep 7779
sudo lsof -i :7779

# Kill or change the port in config/nwdaf.yaml:
# sbi_port: 17779
```

### Integration tests fail: `Connection refused`

The integration-test fixture boots the server in-process on port 17779, so
`Connection refused` usually means a previous test run left a stray
`open5gs-nwdafd` or another ServerFixture on that port. Check:

```bash
pkill open5gs-nwdafd        # Kill any stray production binary
sudo lsof -i :17779         # Anything still on the test port?
cd build && ctest -R "Integration:" --output-on-failure
```

### ABNORMAL_BEHAVIOUR always returns `BASELINE_TOO_LOW`

This is expected on a development machine with no UE traffic. The guard
(controlled by `baseline_stddev_min_kbps` in the config) prevents false
positives on idle data. To exercise the anomaly detection engine directly:

```bash
# The mock tests in test_analytics.cpp inject synthetic traffic.
# Match the Catch2 case names (no tag brackets — they are not tagged):
./build/tests/nwdaf_tests "ABNORMAL_BEHAVIOUR*"     # All 3 ABNORMAL cases
./build/tests/nwdaf_tests "IsolationForest*" -s     # Deterministic IF check
```

### On Open5GS node: NF metrics all show `unknown`

Check that the Open5GS service names in `nwdaf.yaml` match your installation:

```bash
# List all open5gs services
systemctl list-units 'open5gs-*' --no-pager

# Compare with nf_service_names in nwdaf.yaml
# Common difference: some builds use 'open5gs-amf' not 'open5gs-amfd'
```

### On Open5GS node: AMF events always empty

Check AMF log level:

```bash
# Verify AMF is logging at info level
sudo journalctl -u open5gs-amfd -n 20 --no-pager

# If empty or only warnings, check /etc/open5gs/amf.yaml:
# logger:
#   level: info     ← must be info or debug
```

---

## Quick Reference

### Build commands

```bash
# Full clean rebuild (local dev)
rm -rf build && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DNWDAF_USE_SD_JOURNAL=OFF && ninja -j4

# Run all tests
cd build && ctest --output-on-failure

# Run NWDAF locally
./build/open5gs-nwdafd --config config/nwdaf.yaml
```

### Smoke test commands (server must be running on :7779)

```bash
# Health
curl -s http://localhost:7779/nwdaf-analytics/v1/health

# All analytics — show HTTP status and confidence from the envelope
for ID in NF_LOAD UE_MOBILITY UE_COMMUNICATION ABNORMAL_BEHAVIOUR \
          QoS_SUSTAINABILITY SERVICE_EXPERIENCE NETWORK_PERFORMANCE; do
    CODE=$(curl -s -o /tmp/_nwdaf.json -w '%{http_code}' \
        "http://localhost:7779/nwdaf-analytics/v1/analytics?analyticsId=$ID")
    CONF=$(python3 -c 'import sys,json; print(json.load(open("/tmp/_nwdaf.json")).get("confidence","?"))')
    echo "[$ID] HTTP=$CODE confidence=$CONF"
done
```

### 3GPP Compliance summary

| Specification | Feature | Status |
|---|---|---|
| TS 23.288 Table 2.1-1 | All 7 Analytics IDs | ✅ |
| TS 29.520 §5.2 | Nnwdaf_AnalyticsInfo (GET + envelope) | ✅ |
| TS 29.520 §5.3 | Nnwdaf_EventsSubscription CRUD + list | ✅ Store; ⚠️ no push delivery |
| TS 29.510 §5.2.2 | NRF NFRegister (PUT /nf-instances/{id}) | ✅ One-shot on startup |
| TS 28.554 | KPI collection via `/sys/class/net` counters | ✅ |
| systemd | `Type=notify` + `WatchdogSec` readiness | ✅ when built with `NWDAF_USE_SD_JOURNAL=ON` |
| ML | Isolation Forest (anomaly) + EWMA (QoS forecast) | ✅ persisted to `model_dir` as JSON |

---

*Open5GS NWDAF C++ — community implementation*
*Derived from Python reference: github.com/cem8kaya/5g-ai-lab*
*References: 3GPP TS 23.288 v17 · TS 29.520 v17 · TS 29.510 v17 · TS 28.554 v17*
