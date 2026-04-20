# C++ NWDAF Build Prompt
## Generic Open5GS NWDAF — 3GPP TS 23.288 / TS 29.520

---

## CONTEXT

You are building a **production-grade, standalone C++ implementation** of the
**Network Data Analytics Function (NWDAF)** for Open5GS, fully compliant with
3GPP TS 23.288 v17 and TS 29.520 v17. This is a **community-publishable**
reference implementation designed to be portable to any Open5GS deployment.

The reference design is derived from a working Python/Flask NWDAF sidecar that
has been validated on Open5GS v2.7.6 + UERANSIM on Ubuntu 22.04. The C++
port must preserve all functional capabilities, eliminate the Python runtime
dependency, and run as a native systemd service (`open5gs-nwdafd`).

---

## PROJECT LAYOUT

```
open5gs-nwdaf/
├── CMakeLists.txt
├── vcpkg.json                        # Optional: vcpkg manifest
├── .vscode/
│   ├── c_cpp_properties.json
│   ├── tasks.json
│   └── launch.json
├── include/
│   ├── nwdaf_config.hpp              # Runtime config + constants
│   ├── nwdaf_collector.hpp           # Data collection interface
│   ├── nwdaf_analytics.hpp           # Analytics engine interface
│   ├── nwdaf_server.hpp              # HTTP SBI server interface
│   ├── nwdaf_subscription.hpp        # Subscription store interface
│   └── ml/
│       ├── isolation_forest.hpp      # Isolation Forest (C++ native)
│       └── ewma_predictor.hpp        # EWMA throughput predictor
├── src/
│   ├── main.cpp                      # Entry point, systemd sd_notify
│   ├── nwdaf_config.cpp
│   ├── nwdaf_collector.cpp           # journald + /proc + /sys + MongoDB
│   ├── nwdaf_analytics.cpp           # 7 Analytics ID engines
│   ├── nwdaf_server.cpp              # cpp-httplib HTTP server
│   ├── nwdaf_subscription.cpp        # Thread-safe subscription store
│   └── ml/
│       ├── isolation_forest.cpp
│       └── ewma_predictor.cpp
├── tests/
│   ├── CMakeLists.txt
│   ├── test_collector.cpp
│   ├── test_analytics.cpp
│   ├── test_server_integration.cpp   # End-to-end curl-style tests
│   └── mock_open5gs.cpp              # Mock journald + /proc for unit tests
├── systemd/
│   └── open5gs-nwdafd.service        # systemd unit file
├── config/
│   └── nwdaf.yaml                    # Default runtime config
└── README.md
```

---

## DEPENDENCIES

| Library | Purpose | Minimum Version |
|---------|---------|-----------------|
| `cpp-httplib` | Embedded HTTP/1.1 server and client (header-only) | v0.15 |
| `nlohmann/json` | JSON serialization/deserialization (header-only) | v3.11 |
| `yaml-cpp` | `nwdaf.yaml` config file parsing | v0.8 |
| `mongocxx` (libmongoc + libmongocxx) | MongoDB subscriber count query | v3.10 |
| `libsystemd` (`sd-journal`, `sd_notify`) | journald reading + systemd watchdog | system package |
| `spdlog` | Structured logging (console + file) | v1.13 |
| `Catch2` | Unit + integration test framework | v3.5 |
| `nlohmann/json` | Already listed — also used in tests | — |

**No external ML library required.** Isolation Forest and EWMA are implemented
natively (see ML section below).

CMake minimum version: **3.22**. C++ standard: **C++17**.

---

## CONFIGURATION (`config/nwdaf.yaml`)

```yaml
nwdaf:
  # NF identity
  nf_instance_id: ""          # auto-generated UUID if empty
  plmn_mcc: "999"
  plmn_mnc: "70"
  sbi_bind_address: "127.0.0.1"
  sbi_port: 7779              # Must not conflict with Open5GS NFs (port 7777)

  # Open5GS NF service names (systemd unit suffix after 'open5gs-')
  # This map is the critical portability layer — adjust for your Open5GS version
  nf_service_names:
    AMF:  "amfd"
    SMF:  "smfd"
    UPF:  "upfd"
    AUSF: "ausfd"
    UDM:  "udmd"
    PCF:  "pcfd"
    NRF:  "nrfd"
    UDR:  "udrd"
    BSF:  "bsfd"
    NSSF: "nssfd"

  # Data collection
  throughput_interfaces:      # /sys/class/net/<iface>/statistics/
    - "ogstun"
    - "ogstun2"
    - "ogstun3"
    - "ens4"
  throughput_history_size: 360      # 1 hour at 10s interval
  collection_interval_seconds: 10

  # AMF journald log parsing — version-sensitive regex
  # Open5GS v2.7.6: UE identifier is "imsi-XXXXXXXXXXXXXXX"
  # Adjust supi_regex for earlier versions that use "SUPI"
  amf_journal_lines: 500
  smf_journal_lines: 500
  supi_regex: "imsi-(\\d{15})"

  # MongoDB (for subscriber count via UDM)
  mongodb_uri: "mongodb://127.0.0.1:27017"
  mongodb_db:  "open5gs"

  # NRF registration (one-shot on startup)
  nrf_uri: "http://127.0.0.1:7777"
  nrf_register_on_startup: true

  # ML
  model_dir: "/opt/nwdaf/models"
  anomaly_contamination: 0.10
  anomaly_min_samples: 10
  baseline_stddev_min_kbps: 0.5    # Below this, return BASELINE_TOO_LOW
  ewma_alpha: 0.3                   # EWMA smoothing factor

  # Logging
  log_level: "info"                 # trace|debug|info|warn|error
  log_file: "/var/log/open5gs/nwdaf.log"
```

---

## CLASS SPECIFICATIONS

### `NwdafConfig`

```cpp
// include/nwdaf_config.hpp
class NwdafConfig {
public:
    static NwdafConfig load(const std::string& yaml_path);

    std::string nf_instance_id;
    std::string plmn_mcc, plmn_mnc;
    std::string sbi_bind_address;
    int         sbi_port;

    std::map<std::string, std::string> nf_service_names; // "AMF" -> "amfd"
    std::vector<std::string> throughput_interfaces;
    int    throughput_history_size;
    int    collection_interval_seconds;
    int    amf_journal_lines;
    int    smf_journal_lines;
    std::string supi_regex;

    std::string mongodb_uri;
    std::string mongodb_db;

    std::string nrf_uri;
    bool        nrf_register_on_startup;

    std::string model_dir;
    double      anomaly_contamination;
    int         anomaly_min_samples;
    double      baseline_stddev_min_kbps;
    double      ewma_alpha;

    std::string log_level;
    std::string log_file;
};
```

---

### `NwdafCollector`

```cpp
// include/nwdaf_collector.hpp
struct AmfEvent {
    std::string event_type;   // REGISTRATION | DEREGISTRATION | AUTH_SUCCESS |
                              // AUTH_FAILURE | HANDOVER
    std::string supi;
    std::string raw_line;
    std::string timestamp_iso;
};

struct SmfEvent {
    std::string event_type;   // PDU_ESTABLISHED | PDU_RELEASED | QOS_FLOW_CHANGE
    std::string raw_line;
    std::string timestamp_iso;
};

struct ThroughputSample {
    std::string timestamp_iso;
    double      total_dl_bps;
    double      total_ul_bps;
    double      total_dl_kbps;
    double      total_ul_kbps;
    std::map<std::string, std::pair<double,double>> per_iface; // iface -> {rx_bps, tx_bps}
};

struct NfMetric {
    std::string nf_type;      // AMF | SMF | UPF | ...
    std::string status;       // active | inactive | failed | unknown
    int         pid;
    double      cpu_seconds;
    long        mem_kb;
    double      load_pct;
    std::string load_label;   // LOW | MEDIUM | HIGH | OVERLOADED
};

class NwdafCollector {
public:
    explicit NwdafCollector(const NwdafConfig& config);
    ~NwdafCollector();

    // Blocking collectors — called from background thread
    std::vector<AmfEvent>      collectAmfEvents();
    std::vector<SmfEvent>      collectSmfEvents();
    ThroughputSample           collectUPFThroughput();
    std::vector<NfMetric>      collectNfLoad();
    int                        getSubscriberCount();

    // Background thread management
    void startBackgroundCollection();
    void stopBackgroundCollection();

    // Thread-safe accessors for cached data
    std::vector<AmfEvent>         getRecentAmfEvents(int n = 100) const;
    std::vector<SmfEvent>         getRecentSmfEvents(int n = 100) const;
    std::vector<ThroughputSample> getThroughputHistory(int n = 60) const;
    std::vector<NfMetric>         getCachedNfMetrics() const;

private:
    NwdafConfig config_;

    // Thread-safe internal state
    mutable std::mutex                 mutex_;
    std::deque<AmfEvent>               amf_events_;
    std::deque<SmfEvent>               smf_events_;
    std::deque<ThroughputSample>       throughput_history_;
    std::vector<NfMetric>              nf_metrics_;

    // Background thread
    std::thread                        bg_thread_;
    std::atomic<bool>                  running_{false};
    void                               bgLoop();

    // journald helpers
    std::vector<std::string>           readJournalLines(const std::string& unit, int n);

    // /proc helpers
    std::pair<long,long>               readProcStat(int pid);   // {utime, stime}
    long                               readProcMemKb(int pid);

    // /sys/class/net helpers
    std::pair<uint64_t,uint64_t>       readNetStats(const std::string& iface); // {rx_bytes, tx_bytes}

    // MongoDB client (optional, may be nullptr)
    std::unique_ptr<mongocxx::client>  mongo_client_;
    void                               initMongo();
};
```

**Implementation notes for `NwdafCollector`:**

1. **journald reading**: Use the POSIX `sd_journal_open` / `sd_journal_seek_tail` /
   `sd_journal_previous` / `sd_journal_get_data` API from `<systemd/sd-journal.h>`.
   Alternatively, if `libsystemd` is unavailable in the build environment (Ubuntu
   20.04 dev machine), fall back to `popen("journalctl -u open5gs-amfd -n 500 --no-pager")`.
   **Provide both paths with a compile-time flag `NWDAF_USE_SD_JOURNAL`.**

2. **SUPI regex**: Use `std::regex` with the pattern from config (default:
   `imsi-(\d{15})`). Open5GS v2.7.6 does **not** log the word "SUPI" — all
   UE identifiers appear as `imsi-999700000000001`. Do not hardcode this pattern.

3. **NF name mapping**: The `nf_service_names` map in config drives the
   `systemctl is-active` / `MainPID` lookups. Never derive NF type labels by
   string-stripping the service name — `udmd` → "UM" is a known bug.

4. **Throughput measurement**: Read `/sys/class/net/<iface>/statistics/rx_bytes`
   and `tx_bytes`, sleep 1 second, read again, compute delta × 8 = bps. This
   is the only reliable method — `tcpdump` and eBPF return 0 packets due to the
   `gtp5g` kernel module bypassing user-space capture.

5. **Background thread interval**: Default 10 seconds. The thread calls
   `collectUPFThroughput()`, `collectAmfEvents()`, `collectSmfEvents()`, and
   `collectNfLoad()` on each tick and appends results to the respective deques.
   Deque sizes are bounded by `throughput_history_size`.

---

### `IsolationForest` (native C++ ML)

```cpp
// include/ml/isolation_forest.hpp
class IsolationForest {
public:
    explicit IsolationForest(int n_trees = 100, double contamination = 0.10,
                             unsigned int random_seed = 42);

    // Train on 2D feature matrix (rows = samples, cols = [dl_kbps, ul_kbps])
    void fit(const std::vector<std::array<double,2>>& X);

    // Returns +1 (normal) or -1 (anomaly) for each sample
    std::vector<int> predict(const std::vector<std::array<double,2>>& X) const;

    // Returns anomaly score ∈ (-∞, 0]: more negative = more anomalous
    std::vector<double> scoresSamples(const std::vector<std::array<double,2>>& X) const;

    bool isFitted() const;

    // Persistence — binary format (custom or JSON)
    void save(const std::string& path) const;
    void load(const std::string& path);

private:
    struct IsolationTree {
        struct Node {
            bool   is_leaf = false;
            int    split_feature = 0;
            double split_value = 0.0;
            int    left = -1, right = -1;
            int    size = 0;           // leaf: number of samples isolated here
        };
        std::vector<Node> nodes;
        int root = 0;

        void build(const std::vector<std::array<double,2>>& X,
                   int max_depth, std::mt19937& rng);
        double pathLength(const std::array<double,2>& x) const;
    };

    int                       n_trees_;
    double                    contamination_;
    std::mt19937              rng_;
    std::vector<IsolationTree> trees_;
    double                    threshold_ = 0.0;   // score threshold for predict()
    bool                      fitted_ = false;

    double avgPathLength(int n) const;   // c(n) normalisation factor
    double anomalyScore(const std::array<double,2>& x) const;
};
```

**Algorithm notes:**
- Implement the standard Isolation Forest algorithm (Liu et al., 2008).
- Sub-sampling: use 256 samples per tree (standard default).
- Max tree depth: `ceil(log2(min(256, N)))` where N is dataset size.
- Anomaly score formula: `s(x,n) = 2^(-E(h(x)) / c(n))` where `c(n)` is the
  average path length of unsuccessful search in a BST.
- Threshold calibration: after `fit()`, score all training samples, sort
  descending, set `threshold_` at the `contamination * 100`-th percentile.
- The `predict()` method returns -1 for samples with score < threshold_.

---

### `EwmaPredictor` (native C++ ML)

```cpp
// include/ml/ewma_predictor.hpp
class EwmaPredictor {
public:
    explicit EwmaPredictor(double alpha = 0.3);

    // Ingests a new observation; returns updated smoothed value
    double update(double observation);

    // Returns next-step prediction (= current smoothed value)
    double predict() const;

    // Returns stddev of residuals over last N observations
    double residualStddev(int n = 20) const;

    bool hasHistory() const;
    void reset();

private:
    double                 alpha_;
    double                 smoothed_ = 0.0;
    bool                   initialized_ = false;
    std::deque<double>     history_;
    std::deque<double>     residuals_;
};
```

---

### `NwdafAnalyticsEngine`

```cpp
// include/nwdaf_analytics.hpp
using json = nlohmann::json;

class NwdafAnalyticsEngine {
public:
    explicit NwdafAnalyticsEngine(NwdafCollector& collector,
                                  const NwdafConfig& config);

    // Dispatch to the appropriate analytics engine
    // analyticsId: one of the 7 TS 23.288 Table 2.1-1 IDs
    // Optional params: supi, start_ts, end_ts
    json compute(const std::string& analytics_id,
                 const std::string& supi = "",
                 const std::string& start_ts = "",
                 const std::string& end_ts = "");

    static const std::set<std::string> VALID_ANALYTICS_IDS;

private:
    NwdafCollector&  collector_;
    NwdafConfig      config_;
    IsolationForest  anomaly_model_;
    EwmaPredictor    dl_ewma_;
    EwmaPredictor    ul_ewma_;

    json nfLoad(const std::string& supi, const std::string& start_ts, const std::string& end_ts);
    json ueMobility(const std::string& supi, const std::string& start_ts, const std::string& end_ts);
    json ueCommunication(const std::string& supi, const std::string& start_ts, const std::string& end_ts);
    json abnormalBehaviour(const std::string& supi, const std::string& start_ts, const std::string& end_ts);
    json qosSustainability(const std::string& supi, const std::string& start_ts, const std::string& end_ts);
    json serviceExperience(const std::string& supi, const std::string& start_ts, const std::string& end_ts);
    json networkPerformance(const std::string& supi, const std::string& start_ts, const std::string& end_ts);

    void loadModels();
    void saveModels();
    std::string nowISO() const;
};
```

**Analytics ID specifications (must match Python reference exactly):**

#### `NF_LOAD` — TS 23.288 §6.5
```json
{
  "analyticsId": "NF_LOAD",
  "ts": "<ISO8601>",
  "nfLoadLevelList": [
    {
      "nfType": "AMF",
      "nfStatus": "active",
      "nfLoadLevelInfo": {
        "nfLoadLevel": 15.0,
        "nfLoadLevelLabel": "LOW",
        "nfCpuUsage": 12.3,
        "nfMemoryUsage": 45678
      }
    }
  ],
  "overloadedNfs": [],
  "recommendation": "STABLE",
  "confidence": 90
}
```
- Load labels: >80% → OVERLOADED, >60% → HIGH, >30% → MEDIUM, else → LOW
- Compute `load_pct = min(100, cpu_seconds / 10)` (mirrors Python reference)

#### `UE_MOBILITY` — TS 23.288 §6.7
```json
{
  "analyticsId": "UE_MOBILITY",
  "ts": "<ISO8601>",
  "supi": "ALL",
  "registrationCount": 4,
  "deregistrationCount": 2,
  "handoverCount": 0,
  "authFailureCount": 0,
  "mobilityPattern": "LOW",
  "confidence": 85
}
```
- `mobilityPattern`: "HIGH" if (registrations + handovers) > 20, else "LOW"
- If `supi` param provided, filter AMF events by that SUPI

#### `UE_COMMUNICATION` — TS 23.288 §6.6
```json
{
  "analyticsId": "UE_COMMUNICATION",
  "ts": "<ISO8601>",
  "pduSessionEstCount": 3,
  "pduSessionRelCount": 1,
  "activePduSessions": 2,
  "totalSubscribers": 5,
  "currentDlKbps": 128.5,
  "currentUlKbps": 64.2,
  "confidence": 88
}
```

#### `ABNORMAL_BEHAVIOUR` — TS 23.288 §6.3
```json
{
  "analyticsId": "ABNORMAL_BEHAVIOUR",
  "ts": "<ISO8601>",
  "anomalyDetected": true,
  "anomalyPct": 8.3,
  "anomalyType": "UNEXPECTED_LARGE_RATE",
  "anomalyIndices": [12, 37],
  "avgAnomalyScore": -0.4231,
  "dataPoints": 48,
  "baselineDlStd": 42.7,
  "confidence": 92
}
```
- If history < 10 samples: return `"reason": "INSUFFICIENT_DATA"` (confidence 0)
- If DL and UL stddev both < `baseline_stddev_min_kbps`: return `"reason": "BASELINE_TOO_LOW"`
- `anomalyType` classification:
  - DL stddev > 50 → `UNEXPECTED_LARGE_RATE`
  - anomalyPct > 20 → `UNEXPECTED_WAKEUP`
  - otherwise → `SUSPICION_OF_DDOS_ATTACK`

#### `QoS_SUSTAINABILITY` — TS 23.288 §6.12
```json
{
  "analyticsId": "QoS_SUSTAINABILITY",
  "ts": "<ISO8601>",
  "currentDlKbps": 128.5,
  "currentUlKbps": 64.2,
  "predictedDlKbps": 130.1,
  "predictedUlKbps": 65.0,
  "dlTrend": "STABLE",
  "ulTrend": "STABLE",
  "violationRisk": "LOW",
  "confidence": 80
}
```
- EWMA prediction over last 60 samples
- `trend`: delta > +10% → "INCREASING", delta < -10% → "DECREASING", else "STABLE"
- `violationRisk`: predictedDl < 10 Kbps → "HIGH", < 50 Kbps → "MEDIUM", else "LOW"

#### `SERVICE_EXPERIENCE` — TS 23.288 §6.4
```json
{
  "analyticsId": "SERVICE_EXPERIENCE",
  "ts": "<ISO8601>",
  "mosScore": 3.8,
  "mosCategory": "GOOD",
  "dlKbps": 128.5,
  "ulKbps": 64.2,
  "activeSessionRatio": 0.4,
  "confidence": 75
}
```
- MOS scoring: dl > 1000 → 4.5, dl > 500 → 4.0, dl > 100 → 3.5, dl > 50 → 3.0, else 2.0
- Category: MOS > 4 → "EXCELLENT", > 3.5 → "GOOD", > 2.5 → "FAIR", else "POOR"

#### `NETWORK_PERFORMANCE` — TS 23.288 §6.8
```json
{
  "analyticsId": "NETWORK_PERFORMANCE",
  "ts": "<ISO8601>",
  "overallScore": 87.5,
  "scoreLabel": "GOOD",
  "components": {
    "nfHealthScore": 95.0,
    "dlScore": 80.0,
    "pduScore": 90.0
  },
  "confidence": 82
}
```
- Weighted composite: `0.6 * nfHealthScore + 0.2 * dlScore + 0.2 * pduScore`
- nfHealthScore: (active NFs / total NFs) × 100
- dlScore: min(100, currentDlKbps / 10)
- pduScore: min(100, activePduSessions × 20)
- Labels: >90 → "EXCELLENT", >75 → "GOOD", >50 → "FAIR", else "POOR"

---

### `NwdafServer`

```cpp
// include/nwdaf_server.hpp
class NwdafServer {
public:
    NwdafServer(NwdafAnalyticsEngine& engine,
                NwdafSubscriptionStore& subs,
                const NwdafConfig& config);

    void start();   // Blocking — call from main() after sd_notify(READY=1)
    void stop();

private:
    httplib::Server svr_;

    void setupRoutes();

    // GET /nwdaf-analytics/v1/health
    void handleHealth(const httplib::Request&, httplib::Response&);

    // GET /nwdaf-analytics/v1/analytics?analyticsId=...
    void handleGetAnalytics(const httplib::Request&, httplib::Response&);

    // POST /nwdaf-analytics/v1/subscriptions
    void handleCreateSubscription(const httplib::Request&, httplib::Response&);

    // GET /nwdaf-analytics/v1/subscriptions/:subId
    void handleGetSubscription(const httplib::Request&, httplib::Response&);

    // DELETE /nwdaf-analytics/v1/subscriptions/:subId
    void handleDeleteSubscription(const httplib::Request&, httplib::Response&);

    // GET /nwdaf-analytics/v1/subscriptions  (list all)
    void handleListSubscriptions(const httplib::Request&, httplib::Response&);

    // POST /nwdaf-analytics/v1/train  (trigger Isolation Forest retraining)
    void handleTrainModel(const httplib::Request&, httplib::Response&);

    NwdafAnalyticsEngine&  engine_;
    NwdafSubscriptionStore& subs_;
    NwdafConfig            config_;
};
```

**HTTP response format (TS 29.520 §5.2):**
- All responses: `Content-Type: application/json`
- Error format: `{"title": "...", "status": <int>, "cause": "..."}`
- Analytics response envelope:
```json
{
  "analyticsId": "NF_LOAD",
  "requestTime": "<ISO8601>",
  "timeStampGen": "<ISO8601>",
  "validity": 60,
  "confidence": 90,
  "analData": { ... }
}
```

---

### `NwdafSubscriptionStore`

```cpp
// include/nwdaf_subscription.hpp
struct Subscription {
    std::string sub_id;
    std::string analytics_id;
    std::string notif_uri;
    std::string notif_id;
    int         rep_period_seconds;
    int         max_report_nbr;
    std::string created_at_iso;
    std::string status;           // "ACTIVE" | "INACTIVE"
};

class NwdafSubscriptionStore {
public:
    std::string    create(const json& body);          // returns sub_id
    bool           exists(const std::string& sub_id) const;
    Subscription   get(const std::string& sub_id) const;
    bool           remove(const std::string& sub_id);
    std::vector<Subscription> listAll() const;
    int            count() const;

private:
    mutable std::mutex                       mutex_;
    std::unordered_map<std::string, Subscription> store_;
};
```

---

### `main.cpp` entry point

```cpp
int main(int argc, char* argv[]) {
    // 1. Parse CLI args: --config <path>  (default: /etc/open5gs/nwdaf.yaml)
    // 2. Load NwdafConfig
    // 3. Initialize spdlog (level + file from config)
    // 4. Construct NwdafCollector, NwdafAnalyticsEngine, NwdafSubscriptionStore
    // 5. Start background collection thread (NwdafCollector::startBackgroundCollection)
    // 6. Optional NRF registration (HTTP POST to NRF, one-shot)
    // 7. sd_notify(0, "READY=1")           // Inform systemd we are ready
    // 8. NwdafServer::start()              // Blocking
    // 9. On SIGTERM/SIGINT: stop collector, stop server, sd_notify(0, "STOPPING=1")
}
```

---

## `CMakeLists.txt` (root)

```cmake
cmake_minimum_required(VERSION 3.22)
project(open5gs-nwdaf VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(NWDAF_USE_SD_JOURNAL "Use libsystemd sd-journal API (requires systemd dev headers)" ON)
option(NWDAF_BUILD_TESTS    "Build unit and integration tests" ON)

# ── Dependencies ────────────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)

# cpp-httplib (header-only) — fetch via FetchContent or system path
include(FetchContent)
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.15.3)
FetchContent_MakeAvailable(httplib)

# nlohmann/json (header-only)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(json)

# yaml-cpp
find_package(yaml-cpp REQUIRED)

# spdlog
find_package(spdlog REQUIRED)

# libsystemd
if(NWDAF_USE_SD_JOURNAL)
    pkg_check_modules(SYSTEMD REQUIRED libsystemd)
    add_compile_definitions(NWDAF_USE_SD_JOURNAL)
endif()

# mongocxx (optional — graceful degradation if not found)
find_package(mongocxx QUIET)
if(mongocxx_FOUND)
    add_compile_definitions(NWDAF_HAS_MONGODB)
    message(STATUS "MongoDB C++ driver found — subscriber count enabled")
else()
    message(WARNING "MongoDB C++ driver not found — subscriber count disabled (returns 0)")
endif()

# ── NWDAF library ────────────────────────────────────────────────────────────
add_library(nwdaf_lib
    src/nwdaf_config.cpp
    src/nwdaf_collector.cpp
    src/nwdaf_analytics.cpp
    src/nwdaf_server.cpp
    src/nwdaf_subscription.cpp
    src/ml/isolation_forest.cpp
    src/ml/ewma_predictor.cpp
)

target_include_directories(nwdaf_lib PUBLIC include)
target_link_libraries(nwdaf_lib PUBLIC
    httplib::httplib
    nlohmann_json::nlohmann_json
    yaml-cpp
    spdlog::spdlog
)

if(NWDAF_USE_SD_JOURNAL)
    target_link_libraries(nwdaf_lib PUBLIC ${SYSTEMD_LIBRARIES})
    target_include_directories(nwdaf_lib PUBLIC ${SYSTEMD_INCLUDE_DIRS})
endif()

if(mongocxx_FOUND)
    target_link_libraries(nwdaf_lib PUBLIC mongo::mongocxx_shared)
endif()

# ── Main executable ──────────────────────────────────────────────────────────
add_executable(open5gs-nwdafd src/main.cpp)
target_link_libraries(open5gs-nwdafd PRIVATE nwdaf_lib)

install(TARGETS open5gs-nwdafd DESTINATION /usr/local/bin)
install(FILES config/nwdaf.yaml DESTINATION /etc/open5gs)
install(FILES systemd/open5gs-nwdafd.service DESTINATION /etc/systemd/system)

# ── Tests ────────────────────────────────────────────────────────────────────
if(NWDAF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

---

## `systemd/open5gs-nwdafd.service`

```ini
[Unit]
Description=Open5GS NWDAF - Network Data Analytics Function (3GPP TS 23.288)
Documentation=https://open5gs.org https://github.com/cem8kaya/5g-ai-lab
After=network.target open5gs-amfd.service open5gs-smfd.service
Wants=open5gs-amfd.service open5gs-smfd.service

[Service]
Type=notify
ExecStart=/usr/local/bin/open5gs-nwdafd --config /etc/open5gs/nwdaf.yaml
ExecReload=/bin/kill -HUP $MAINPID
Restart=on-failure
RestartSec=5
User=root
StandardOutput=journal
StandardError=journal
SyslogIdentifier=open5gs-nwdafd
WatchdogSec=30

[Install]
WantedBy=multi-user.target
```

---

## TEST SPECIFICATIONS (`tests/`)

### Unit tests — `test_analytics.cpp`

1. `TEST_CASE("NF_LOAD returns STABLE when all NFs active")` — mock 7 NFs all
   active, assert `recommendation == "STABLE"`.
2. `TEST_CASE("NF_LOAD returns SCALE_OUT when one NF OVERLOADED")` — mock one
   NF at 95% load, assert `recommendation == "SCALE_OUT"`.
3. `TEST_CASE("ABNORMAL_BEHAVIOUR returns INSUFFICIENT_DATA below 10 samples")`
4. `TEST_CASE("ABNORMAL_BEHAVIOUR returns BASELINE_TOO_LOW on idle traffic")`
   — all samples = 0.1 Kbps, assert `reason == "BASELINE_TOO_LOW"`.
5. `TEST_CASE("ABNORMAL_BEHAVIOUR detects injected spike")` — 40 normal samples
   + 5 spike samples (10× normal), assert `anomalyDetected == true`.
6. `TEST_CASE("QoS_SUSTAINABILITY EWMA converges after 20 samples")`
7. `TEST_CASE("NETWORK_PERFORMANCE score with 6/7 NFs active")` — assert score
   in [80, 100] range.
8. `TEST_CASE("IsolationForest: outliers score lower than inliers")`
9. `TEST_CASE("EwmaPredictor: prediction tracks step change with lag")`

### Integration tests — `test_server_integration.cpp`

Start the server on port 17779 (test port), issue real HTTP requests:

1. `GET /nwdaf-analytics/v1/health` → 200, `status == "UP"`
2. `GET /nwdaf-analytics/v1/analytics?analyticsId=NF_LOAD` → 200, has
   `nfLoadLevelList` array
3. `GET /nwdaf-analytics/v1/analytics` (no param) → 400
4. `GET /nwdaf-analytics/v1/analytics?analyticsId=INVALID_ID` → 422
5. `POST /nwdaf-analytics/v1/subscriptions` with valid body → 201, has `subId`
6. `GET /nwdaf-analytics/v1/subscriptions/<subId>` → 200
7. `DELETE /nwdaf-analytics/v1/subscriptions/<subId>` → 204
8. `GET /nwdaf-analytics/v1/subscriptions/<subId>` after delete → 404
9. All 7 analytics IDs return 200 with `analyticsId` field matching request
10. `POST /nwdaf-analytics/v1/train` → 200 (triggers inline IF training)

### Mock helpers — `mock_open5gs.cpp`

Provide mock implementations of:
- `readJournalLines()` — returns pre-built log line vectors
- `readNetStats()` — returns incrementing byte counters
- `readProcStat()` / `readProcMemKb()` — returns fixed PID metrics
- `getSubscriberCount()` (MongoDB mock) — returns fixed count

---

## VS CODE CONFIGURATION

### `.vscode/c_cpp_properties.json`
```json
{
  "configurations": [
    {
      "name": "Linux",
      "includePath": [
        "${workspaceFolder}/include/**",
        "${workspaceFolder}/build/_deps/httplib-src",
        "${workspaceFolder}/build/_deps/json-src/include",
        "/usr/include/spdlog",
        "/usr/include/mongocxx/v_noabi",
        "/usr/include/bsoncxx/v_noabi"
      ],
      "compilerPath": "/usr/bin/g++",
      "cppStandard": "c++17",
      "intelliSenseMode": "linux-gcc-x64"
    }
  ],
  "version": 4
}
```

### `.vscode/tasks.json`
```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "CMake Configure",
      "type": "shell",
      "command": "cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DNWDAF_USE_SD_JOURNAL=OFF",
      "group": "build",
      "problemMatcher": []
    },
    {
      "label": "CMake Build",
      "type": "shell",
      "command": "cmake --build build --parallel 4",
      "group": { "kind": "build", "isDefault": true },
      "problemMatcher": "$gcc"
    },
    {
      "label": "Run Tests",
      "type": "shell",
      "command": "cd build && ctest --output-on-failure",
      "group": { "kind": "test", "isDefault": true },
      "problemMatcher": []
    },
    {
      "label": "Run NWDAF (local dev)",
      "type": "shell",
      "command": "./build/open5gs-nwdafd --config config/nwdaf.yaml",
      "problemMatcher": []
    }
  ]
}
```

### `.vscode/launch.json`
```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug NWDAF",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/build/open5gs-nwdafd",
      "args": ["--config", "${workspaceFolder}/config/nwdaf.yaml"],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}",
      "environment": [],
      "externalConsole": false,
      "MIMode": "gdb",
      "setupCommands": [
        {
          "description": "Enable pretty-printing for gdb",
          "text": "-enable-pretty-printing",
          "ignoreFailures": true
        }
      ],
      "preLaunchTask": "CMake Build"
    },
    {
      "name": "Debug Tests",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/build/tests/nwdaf_tests",
      "args": [],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}",
      "MIMode": "gdb",
      "preLaunchTask": "CMake Build"
    }
  ]
}
```

---

## PORTABILITY REQUIREMENTS

The implementation must be adaptable to any Open5GS deployment without code
changes. All deployment-specific values must come from `nwdaf.yaml`:

1. **PLMN** — `plmn_mcc` / `plmn_mnc` (default 999/70 for test PLMN)
2. **NF service names** — `nf_service_names` map
3. **UPF interfaces** — `throughput_interfaces` list
4. **SUPI regex** — `supi_regex` (for Open5GS version compatibility)
5. **MongoDB URI** — `mongodb_uri` + `mongodb_db`
6. **NRF URI** — `nrf_uri`
7. **Port** — `sbi_port` (default 7779; must not conflict with 7777)

Include graceful degradation:
- If MongoDB is unavailable: subscriber count returns 0, no crash
- If a journald unit is not found: return empty event list, log warning
- If `/sys/class/net/<iface>` not found: skip that interface silently
- If model files not found at startup: operate in rule-based mode until
  `/nwdaf-analytics/v1/train` is called

---

## KNOWN CONSTRAINTS TO PRESERVE FROM PYTHON REFERENCE

These issues were discovered in production and **must not be reintroduced**:

| # | Issue | Resolution |
|---|-------|-----------|
| 1 | NF name mapping bug: `udmd` → "UM" via string stripping | Use explicit `nf_service_names` map |
| 2 | AMF regex: Open5GS v2.7.6 uses `imsi-` not `SUPI` | Configurable `supi_regex` |
| 3 | gtp5g kernel bypass: uesimtun0 RX always 0 | Use `/sys/class/net/ogstun/statistics/` |
| 4 | Idle baseline: Isolation Forest flags any traffic as anomaly when trained on zeros | `baseline_stddev_min_kbps` guard |
| 5 | NETWORK_PERFORMANCE weight: nfHealth weight too low | Configurable weights or fixed 0.6/0.2/0.2 |

---

## DELIVERABLES CHECKLIST

- [ ] All source files in the directory layout above
- [ ] `CMakeLists.txt` builds cleanly on Ubuntu 20.04 with `cmake -S . -B build -DNWDAF_USE_SD_JOURNAL=OFF`
- [ ] All 9 unit tests pass: `ctest --output-on-failure`
- [ ] All 10 integration tests pass (server on port 17779)
- [ ] All 7 Analytics IDs return valid JSON on mock data
- [ ] `.vscode/` workspace files enable F5 debugging
- [ ] `config/nwdaf.yaml` fully documented
- [ ] `systemd/open5gs-nwdafd.service` ready for `systemctl enable`
- [ ] `README.md` with Quick Start, portability notes, 3GPP compliance table

---

*References: 3GPP TS 23.288 v17.3.0 · TS 29.520 v17.7.0 · TS 29.510 v17.6.0 · TS 28.554 v17.4.0*
