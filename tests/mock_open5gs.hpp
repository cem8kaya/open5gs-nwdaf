#pragma once
#include "nwdaf_collector.hpp"
#include <map>
#include <vector>
#include <string>
#include <cstdint>

class MockNwdafCollector : public NwdafCollector {
public:
    explicit MockNwdafCollector(const NwdafConfig& cfg);

    void setAmfLines(const std::vector<std::string>& lines);
    void setSmfLines(const std::vector<std::string>& lines);
    void setNetStats(const std::string& iface, uint64_t rx, uint64_t tx);
    void setProcStat(int pid, long utime, long stime);
    void setMemKb(int pid, long kb);
    void setSubscriberCount(int n);
    void setNfMetrics(const std::vector<NfMetric>& metrics);

protected:
    std::vector<std::string>         readJournalLines(const std::string& unit, int n) override;
    std::pair<long,long>             readProcStat(int pid) override;
    long                             readProcMemKb(int pid) override;
    std::pair<uint64_t,uint64_t>     readNetStats(const std::string& iface) override;
    int                              querySubscriberCountFromMongo() override;

private:
    std::vector<std::string>                                mock_amf_lines_;
    std::vector<std::string>                                mock_smf_lines_;
    std::map<std::string, std::pair<uint64_t,uint64_t>>     mock_net_stats_;
    std::map<std::string, int>                              net_stats_call_count_;
    std::map<int, std::pair<long,long>>                     mock_proc_stat_;
    std::map<int, long>                                     mock_mem_kb_;
    int                                                     mock_subscriber_count_ = 5;
    std::vector<NfMetric>                                   mock_nf_metrics_;
};
