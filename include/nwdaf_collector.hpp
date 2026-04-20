#pragma once
#include "nwdaf_config.hpp"
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

#ifdef NWDAF_HAS_MONGODB
#include <mongocxx/client.hpp>
#endif

struct AmfEvent {
    std::string event_type;
    std::string supi;
    std::string raw_line;
    std::string timestamp_iso;
};

struct SmfEvent {
    std::string event_type;
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

    std::vector<AmfEvent>         getRecentAmfEvents(int n = 100) const;
    std::vector<SmfEvent>         getRecentSmfEvents(int n = 100) const;
    std::vector<ThroughputSample> getThroughputHistory(int n = 60) const;
    std::vector<NfMetric>         getCachedNfMetrics() const;

protected:
    virtual std::vector<std::string> readJournalLines(const std::string& unit, int n);
    virtual std::pair<long,long>     readProcStat(int pid);
    virtual long                     readProcMemKb(int pid);
    virtual std::pair<uint64_t,uint64_t> readNetStats(const std::string& iface);
    virtual int                      querySubscriberCountFromMongo();

    NwdafConfig config_;

private:
    mutable std::mutex           mutex_;
    std::deque<AmfEvent>         amf_events_;
    std::deque<SmfEvent>         smf_events_;
    std::deque<ThroughputSample> throughput_history_;
    std::vector<NfMetric>        nf_metrics_;

    std::thread        bg_thread_;
    std::atomic<bool>  running_{false};
    void               bgLoop();

    void initMongo();

#ifdef NWDAF_HAS_MONGODB
    std::unique_ptr<mongocxx::client> mongo_client_;
#endif
};
