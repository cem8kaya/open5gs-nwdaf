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
8. [Run Unit Tests](#8-run-unit-tests)
9. [Run Integration Tests](#9-run-integration-tests)
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

### 3.4 MongoDB C++ Driver (optional)

MongoDB is used only for subscriber count. It is optional — if not installed,
the build proceeds and `getSubscriberCount()` always returns 0.

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

Open `config/nwdaf.yaml` and verify these settings for local dev:

```yaml
nwdaf:
  sbi_bind_address: "127.0.0.1"
  sbi_port: 7779

  # These won't match anything on your dev machine — that's fine.
  # Collectors will return empty/zero data; mock tests inject their own data.
  mongodb_uri: "mongodb://127.0.0.1:27017"
  nrf_register_on_startup: false  # Disable for local dev (no NRF available)

  log_level: "debug"
  log_file: ""    # Empty = log to stdout only
```

### 6.2 CMake configuration

The project uses two key flags for local development:

| Flag | Value for local dev | Value for Open5GS node |
|---|---|---|
| `NWDAF_USE_SD_JOURNAL` | `OFF` (use journalctl subprocess) | `ON` (use sd-journal API) |
| `NWDAF_BUILD_TESTS` | `ON` | `ON` or `OFF` |
| `CMAKE_BUILD_TYPE` | `Debug` | `Release` |

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
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DNWDAF_USE_SD_JOURNAL=OFF \
    -DNWDAF_BUILD_TESTS=ON \
    -G Ninja

# Build
ninja -j4

# Or with make:
# cmake .. -DCMAKE_BUILD_TYPE=Debug -DNWDAF_USE_SD_JOURNAL=OFF
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

## 8. Run Unit Tests

### Method A: VS Code CMake Tools

Press `Ctrl+Shift+P` → `CMake: Run Tests`

### Method B: Terminal

```bash
cd build
ctest --output-on-failure --verbose
```

### Expected output

```
Test project /path/to/open5gs-nwdaf/build
    Start 1: TestCollector
1/9 Test #1: TestCollector .............................   Passed    0.02 sec
    Start 2: TestIsolationForest
2/9 Test #2: TestIsolationForest .......................   Passed    0.15 sec
    Start 3: TestEWMAPredictor
3/9 Test #3: TestEWMAPredictor .........................   Passed    0.01 sec
    Start 4: TestAnalytics_NfLoad
4/9 Test #4: TestAnalytics_NfLoad ......................   Passed    0.02 sec
    Start 5: TestAnalytics_AbnormalBehaviour
5/9 Test #5: TestAnalytics_AbnormalBehaviour ...........   Passed    0.18 sec
    Start 6: TestAnalytics_QosSustainability
6/9 Test #6: TestAnalytics_QosSustainability ...........   Passed    0.03 sec
    Start 7: TestAnalytics_NetworkPerformance
7/9 Test #7: TestAnalytics_NetworkPerformance ..........   Passed    0.01 sec
    Start 8: TestAnalytics_AllIds
8/9 Test #8: TestAnalytics_AllIds ......................   Passed    0.04 sec
    Start 9: TestSubscriptionStore
9/9 Test #9: TestSubscriptionStore .....................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 9
```

### Run a specific test

```bash
# Run only the Isolation Forest test
cd build && ctest -R TestIsolationForest --output-on-failure

# Run with verbose Catch2 output
./build/tests/nwdaf_tests "[isolation_forest]" -v
```

---

## 9. Run Integration Tests

Integration tests start the NWDAF server on port 17779 (not 7779) and issue
real HTTP requests against it.

```bash
cd build
ctest -R TestServerIntegration --output-on-failure
```

Or run directly:

```bash
./build/tests/nwdaf_integration_tests
```

Expected output:

```
===============================================================================
All tests passed (10 assertions in 10 test cases)
```

### What these tests verify

| # | Test | Asserts |
|---|------|---------|
| 1 | Health endpoint | `status == "UP"`, has `nfProfile` |
| 2 | NF_LOAD | HTTP 200, has `nfLoadLevelList` array |
| 3 | Missing analyticsId | HTTP 400 |
| 4 | Invalid analyticsId | HTTP 422 |
| 5 | Create subscription | HTTP 201, has `subId` |
| 6 | Get subscription | HTTP 200, matches created |
| 7 | Delete subscription | HTTP 204 |
| 8 | Get deleted subscription | HTTP 404 |
| 9 | All 7 Analytics IDs | HTTP 200 each, `analyticsId` field correct |
| 10 | Train endpoint | HTTP 200 |

---

## 10. Manual API Smoke Test

Start the NWDAF in a terminal:

```bash
./build/open5gs-nwdafd --config config/nwdaf.yaml
```

You should see:

```
[2026-04-20 12:00:00.123] [nwdaf] [info] NWDAF starting — 3GPP TS 23.288/29.520 v17
[2026-04-20 12:00:00.124] [nwdaf] [info] Background collection thread started (10s interval)
[2026-04-20 12:00:00.125] [nwdaf] [info] Listening on 127.0.0.1:7779
```

Open a **second terminal** and run these smoke tests:

### Health check

```bash
curl -s http://localhost:7779/nwdaf-analytics/v1/health | python3 -m json.tool
```

Expected:
```json
{
    "status": "UP",
    "nfProfile": {
        "nfType": "NWDAF",
        "nfStatus": "REGISTERED",
        "nwdafInfo": {
            "analyticsIds": ["NF_LOAD", "UE_MOBILITY", ...]
        }
    },
    "timestamp": "...",
    "throughputHistoryLen": 0
}
```

### Query all 7 Analytics IDs

```bash
for ID in NF_LOAD UE_MOBILITY UE_COMMUNICATION ABNORMAL_BEHAVIOUR \
          QoS_SUSTAINABILITY SERVICE_EXPERIENCE NETWORK_PERFORMANCE; do
    echo "=== $ID ==="
    curl -s "http://localhost:7779/nwdaf-analytics/v1/analytics?analyticsId=$ID" \
        | python3 -m json.tool
    echo ""
done
```

### Subscription lifecycle

```bash
# Create subscription
SUB=$(curl -s -X POST http://localhost:7779/nwdaf-analytics/v1/subscriptions \
    -H "Content-Type: application/json" \
    -d '{"analyticsId":"NF_LOAD","notifUri":"http://localhost:9000/notify","notifId":"test-1"}')
echo $SUB | python3 -m json.tool

# Extract subId
SUB_ID=$(echo $SUB | python3 -c "import sys,json; print(json.load(sys.stdin)['subId'])")

# Get subscription
curl -s http://localhost:7779/nwdaf-analytics/v1/subscriptions/$SUB_ID | python3 -m json.tool

# Delete subscription
curl -s -X DELETE http://localhost:7779/nwdaf-analytics/v1/subscriptions/$SUB_ID
echo "Deleted: HTTP $(curl -s -o /dev/null -w '%{http_code}' -X DELETE \
    http://localhost:7779/nwdaf-analytics/v1/subscriptions/$SUB_ID)"
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
2. Set a breakpoint — e.g., in `src/nwdaf_analytics.cpp` in the `_nf_load()`
   function (click the gutter to the left of the line number)
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

The integration test starts the server internally — this error usually means
a previous test run left the server running. Check:

```bash
pkill open5gs-nwdafd   # Kill any stray process
# Re-run tests
cd build && ctest -R TestServerIntegration --output-on-failure
```

### ABNORMAL_BEHAVIOUR always returns `BASELINE_TOO_LOW`

This is expected on a development machine with no UE traffic. The guard is
intentional (prevents false positives on idle data). To test the anomaly
detection engine:

```bash
# The mock test in test_analytics.cpp injects synthetic spike data.
# Run it to verify the algorithm works:
./build/tests/nwdaf_tests "[abnormal_behaviour]" -v
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

# All analytics
for ID in NF_LOAD UE_MOBILITY UE_COMMUNICATION ABNORMAL_BEHAVIOUR \
          QoS_SUSTAINABILITY SERVICE_EXPERIENCE NETWORK_PERFORMANCE; do
    echo "[$ID] $(curl -s "http://localhost:7779/nwdaf-analytics/v1/analytics?analyticsId=$ID" | python3 -c 'import sys,json; d=json.load(sys.stdin); print(d.get("analData",{}).get("recommendation","ok"))')"
done
```

### 3GPP Compliance summary

| Specification | Feature | Status |
|---|---|---|
| TS 23.288 Table 2.1-1 | All 7 Analytics IDs | ✅ |
| TS 29.520 §5.2 | Nnwdaf_AnalyticsInfo (GET) | ✅ |
| TS 29.520 §5.3 | Nnwdaf_EventsSubscription (CRUD) | ✅ Store; ⚠️ no push delivery |
| TS 29.510 §5.2.2 | NRF NFRegister | ✅ One-shot on startup |
| TS 28.554 | KPI collection via /sys counters | ✅ |

---

*Open5GS NWDAF C++ — community implementation*
*Derived from Python reference: github.com/cem8kaya/5g-ai-lab*
*References: 3GPP TS 23.288 v17 · TS 29.520 v17 · TS 29.510 v17 · TS 28.554 v17*
