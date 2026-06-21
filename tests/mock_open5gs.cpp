#include "mock_open5gs.hpp"
#include <chrono>
#include <ctime>

MockNwdafCollector::MockNwdafCollector(const NwdafConfig& cfg) : NwdafCollector(cfg) {}

void MockNwdafCollector::setAmfLines(const std::vector<std::string>& lines) {
    mock_amf_lines_ = lines;
}
void MockNwdafCollector::setSmfLines(const std::vector<std::string>& lines) {
    mock_smf_lines_ = lines;
}
void MockNwdafCollector::setNetStats(const std::string& iface, uint64_t rx, uint64_t tx) {
    mock_net_stats_[iface] = {rx, tx};
    net_stats_call_count_[iface] = 0;
}
void MockNwdafCollector::setProcStat(int pid, long utime, long stime) {
    mock_proc_stat_[pid] = {utime, stime};
}
void MockNwdafCollector::setMemKb(int pid, long kb) {
    mock_mem_kb_[pid] = kb;
}
void MockNwdafCollector::setSubscriberCount(int n) {
    mock_subscriber_count_ = n;
}
void MockNwdafCollector::setNfMetrics(const std::vector<NfMetric>& metrics) {
    mock_nf_metrics_ = metrics;
}

std::vector<std::string> MockNwdafCollector::readJournalLines(const std::string& unit, int n) {
    (void)n;
    if (unit == config_.nf_service_names.at("AMF"))
        return mock_amf_lines_;
    if (unit == config_.nf_service_names.at("SMF"))
        return mock_smf_lines_;
    return {};
}

std::pair<long,long> MockNwdafCollector::readProcStat(int pid) {
    auto it = mock_proc_stat_.find(pid);
    if (it == mock_proc_stat_.end()) return {0, 0};
    return it->second;
}

long MockNwdafCollector::readProcMemKb(int pid) {
    auto it = mock_mem_kb_.find(pid);
    if (it == mock_mem_kb_.end()) return 0;
    return it->second;
}

std::pair<uint64_t,uint64_t> MockNwdafCollector::readNetStats(const std::string& iface) {
    auto it = mock_net_stats_.find(iface);
    if (it == mock_net_stats_.end()) return {0, 0};
    // Increment on each call to simulate traffic
    auto& [rx, tx] = it->second;
    int& count = net_stats_call_count_[iface];
    uint64_t delta = (uint64_t)(count * 10000);
    ++count;
    return {rx + delta, tx + delta / 2};
}

int MockNwdafCollector::querySubscriberCountFromMongo() {
    return mock_subscriber_count_;
}

// BUG-01: injectable clock
void MockNwdafCollector::setMockCpuTime(std::chrono::steady_clock::time_point t) {
    mock_cpu_now_ = t;
    use_mock_cpu_now_ = true;
}

void MockNwdafCollector::advanceMockCpuTime(std::chrono::steady_clock::duration d) {
    mock_cpu_now_ += d;
}

std::chrono::steady_clock::time_point MockNwdafCollector::getCpuNow() const {
    return use_mock_cpu_now_ ? mock_cpu_now_ : std::chrono::steady_clock::now();
}

double MockNwdafCollector::testComputeCpuPct(int pid) {
    return computeCpuPct(pid);
}
