#include "nwdaf_config.hpp"
#include <yaml-cpp/yaml.h>
#include <stdexcept>
#include <random>
#include <sstream>
#include <iomanip>

static std::string generateUUID() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    uint64_t a = dis(gen), b = dis(gen);
    // Set version 4 and variant bits
    a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << (a >> 32) << '-'
        << std::setw(4) << ((a >> 16) & 0xFFFF) << '-'
        << std::setw(4) << (a & 0xFFFF) << '-'
        << std::setw(4) << (b >> 48) << '-'
        << std::setw(12) << (b & 0xFFFFFFFFFFFFULL);
    return oss.str();
}

NwdafConfig NwdafConfig::load(const std::string& yaml_path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(yaml_path);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to load config file '" + yaml_path + "': " + e.what());
    }

    auto n = root["nwdaf"];
    NwdafConfig cfg;

    cfg.nf_instance_id    = n["nf_instance_id"] ? n["nf_instance_id"].as<std::string>() : "";
    if (cfg.nf_instance_id.empty()) cfg.nf_instance_id = generateUUID();

    cfg.plmn_mcc          = n["plmn_mcc"]          ? n["plmn_mcc"].as<std::string>()          : "999";
    cfg.plmn_mnc          = n["plmn_mnc"]          ? n["plmn_mnc"].as<std::string>()          : "70";
    cfg.sbi_bind_address  = n["sbi_bind_address"]  ? n["sbi_bind_address"].as<std::string>()  : "127.0.0.1";
    cfg.sbi_port          = n["sbi_port"]          ? n["sbi_port"].as<int>()                  : 7779;

    if (n["nf_service_names"]) {
        for (const auto& kv : n["nf_service_names"]) {
            cfg.nf_service_names[kv.first.as<std::string>()] = kv.second.as<std::string>();
        }
    } else {
        cfg.nf_service_names = {{"AMF","amfd"},{"SMF","smfd"},{"UPF","upfd"},
                                 {"AUSF","ausfd"},{"UDM","udmd"},{"PCF","pcfd"},
                                 {"NRF","nrfd"},{"UDR","udrd"},{"BSF","bsfd"},{"NSSF","nssfd"}};
    }

    if (n["throughput_interfaces"]) {
        for (const auto& iface : n["throughput_interfaces"])
            cfg.throughput_interfaces.push_back(iface.as<std::string>());
    } else {
        cfg.throughput_interfaces = {"ogstun"};
    }

    cfg.throughput_history_size      = n["throughput_history_size"]      ? n["throughput_history_size"].as<int>()      : 360;
    cfg.collection_interval_seconds  = n["collection_interval_seconds"]  ? n["collection_interval_seconds"].as<int>()  : 10;
    cfg.amf_journal_lines            = n["amf_journal_lines"]            ? n["amf_journal_lines"].as<int>()            : 500;
    cfg.smf_journal_lines            = n["smf_journal_lines"]            ? n["smf_journal_lines"].as<int>()            : 500;
    cfg.supi_regex                   = n["supi_regex"]                   ? n["supi_regex"].as<std::string>()           : "imsi-(\\d{15})";

    cfg.mongodb_uri = n["mongodb_uri"] ? n["mongodb_uri"].as<std::string>() : "mongodb://127.0.0.1:27017";
    cfg.mongodb_db  = n["mongodb_db"]  ? n["mongodb_db"].as<std::string>()  : "open5gs";

    cfg.nrf_uri                  = n["nrf_uri"]                  ? n["nrf_uri"].as<std::string>()     : "http://127.0.0.1:7777";
    cfg.nrf_register_on_startup  = n["nrf_register_on_startup"]  ? n["nrf_register_on_startup"].as<bool>() : true;

    cfg.model_dir                = n["model_dir"]                ? n["model_dir"].as<std::string>()    : "/opt/nwdaf/models";
    cfg.anomaly_contamination    = n["anomaly_contamination"]    ? n["anomaly_contamination"].as<double>()  : 0.10;
    cfg.anomaly_min_samples      = n["anomaly_min_samples"]      ? n["anomaly_min_samples"].as<int>()       : 10;
    cfg.baseline_stddev_min_kbps = n["baseline_stddev_min_kbps"] ? n["baseline_stddev_min_kbps"].as<double>() : 0.5;
    cfg.ewma_alpha               = n["ewma_alpha"]               ? n["ewma_alpha"].as<double>()             : 0.3;

    cfg.log_level = n["log_level"] ? n["log_level"].as<std::string>() : "info";
    cfg.log_file  = n["log_file"]  ? n["log_file"].as<std::string>()  : "/var/log/open5gs/nwdaf.log";

    return cfg;
}
