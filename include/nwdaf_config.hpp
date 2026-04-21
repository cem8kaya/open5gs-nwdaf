#pragma once
#include <string>
#include <map>
#include <vector>

class NwdafConfig {
public:
    static NwdafConfig load(const std::string& yaml_path);

    std::string nf_instance_id;
    std::string plmn_mcc, plmn_mnc;
    std::string sbi_bind_address;
    int         sbi_port;

    std::map<std::string, std::string> nf_service_names;
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
    int         nrf_heartbeat_interval_seconds;

    std::string model_dir;
    double      anomaly_contamination;
    int         anomaly_min_samples;
    double      baseline_stddev_min_kbps;
    double      ewma_alpha;

    std::string log_level;
    std::string log_file;

    // PROD-01: throughput history persistence
    std::string history_backend;    // "none" | "sqlite" | "mongodb"
    std::string history_db_path;    // used when history_backend == "sqlite"

    // PROD-06: rate limiting
    int rate_limit_per_ip_rps;      // 0 = unlimited per-IP
    int rate_limit_global_rps;      // 0 = unlimited global
};
