#pragma once
#include "nwdaf_config.hpp"
#include "ml/ewma_predictor.hpp"
#include <string>
#include <vector>
#include <map>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <cstdint>
#include <utility>
#include <chrono>
#include <functional>
#include <unordered_set>

#ifdef NWDAF_HAS_MONGODB
#include <mongocxx/client.hpp>
#endif

#ifdef NWDAF_HAS_SQLITE
// Forward-declare sqlite3 to avoid including <sqlite3.h> in this header.
struct sqlite3;
#endif

struct AmfEvent {
    std::string event_type;
    std::string supi;
    std::string raw_line;
    std::string timestamp_iso;
};

struct SmfEvent {
    std::string event_type;
    std::string supi;
    // ARCH-04: opaque session key (SUPI or SUPI+DNN) used by the stateful tracker
    std::string session_id;
    std::string raw_line;
    std::string timestamp_iso;
};

struct ThroughputSample {
    std::string timestamp_iso;
    double      total_dl_bps;
    double      total_ul_bps;
    double      total_dl_kbps;
    double      total_ul_kbps;
    std::map<std::string, std::pair<double,double>> per_iface;
};

struct NfMetric {
    std::string nf_type;
    std::string status;
    int         pid;
    double      cpu_seconds;
    long        mem_kb;
    double      load_pct;
    std::string load_label;
};

class NwdafCollector {
public:
    explicit NwdafCollector(const NwdafConfig& config);
    ~NwdafCollector();

    std::vector<AmfEvent>      collectAmfEvents();
    std::vector<SmfEvent>      collectSmfEvents();
    ThroughputSample           collectUPFThroughput();
    std::vector<NfMetric>      collectNfLoad();
    int                        getSubscriberCount();

    void startBackgroundCollection();
    void stopBackgroundCollection();

    // PROD-04: hot-reloadable settings (safe to call from SIGHUP handler)
    void updateConfig(int collection_interval_seconds, double ewma_alpha);

    std::vector<AmfEvent>         getRecentAmfEvents(int n = 100) const;
    std::vector<SmfEvent>         getRecentSmfEvents(int n = 100) const;
    std::vector<ThroughputSample> getThroughputHistory(int n = 60) const;
    std::vector<NfMetric>         getCachedNfMetrics() const;

    // BUG-02: EWMA predictions updated by bgLoop, read-only for analytics
    double getDlEwmaPrediction() const;
    double getUlEwmaPrediction() const;

    // ARCH-04: stateful PDU session count (survives log rotation / service restart)
    int getActivePduSessionCount() const;

protected:
    virtual std::vector<std::string> readJournalLines(const std::string& unit, int n);
    virtual std::pair<long,long>     readProcStat(int pid);
    virtual long                     readProcMemKb(int pid);
    virtual std::pair<uint64_t,uint64_t> readNetStats(const std::string& iface);
    virtual int                      querySubscriberCountFromMongo();

    // BUG-01: injectable clock for testability
    virtual std::chrono::steady_clock::time_point getCpuNow() const;

    // BUG-01: rate-based CPU utilisation (callable by subclass test wrappers)
    double computeCpuPct(int pid);

    NwdafConfig config_;

private:
    mutable std::mutex           mutex_;
    std::deque<AmfEvent>         amf_events_;
    std::deque<SmfEvent>         smf_events_;
    std::deque<ThroughputSample> throughput_history_;
    std::vector<NfMetric>        nf_metrics_;

    // BUG-01: per-PID CPU snapshot for delta computation
    struct CpuSnapshot {
        long ticks;
        std::chrono::steady_clock::time_point ts;
    };
    std::map<int, CpuSnapshot> cpu_snapshots_;

    // BUG-02: EWMA predictors updated once per collection interval
    EwmaPredictor dl_ewma_;
    EwmaPredictor ul_ewma_;

    // ARCH-04: stateful PDU session tracker — persists across log rotation.
    // Keyed by session_id (= SUPI, or SUPI+DNN when extractable).
    std::unordered_set<std::string> active_sessions_;

    // PROD-08: pre-snapshot for non-blocking throughput measurement
    struct NetSnapshot {
        std::map<std::string, std::pair<uint64_t,uint64_t>> readings;
        std::chrono::steady_clock::time_point ts;
    };
    NetSnapshot prev_net_snapshot_;
    bool        has_net_snapshot_ = false;

    std::thread        bg_thread_;
    std::atomic<bool>  running_{false};
    void               bgLoop();

    void initMongo();

#ifdef NWDAF_HAS_MONGODB
    std::unique_ptr<mongocxx::client> mongo_client_;
#endif

    // PROD-01: optional SQLite-backed throughput history persistence
#ifdef NWDAF_HAS_SQLITE
    sqlite3* history_db_ = nullptr;
    void initHistoryDb();
    void persistThroughputSample(const ThroughputSample& s);
    void warmFromHistoryDb();
#else
    void warmFromHistoryDb() {}  // no-op when SQLite not compiled in
#endif
};
