#include "nwdaf_config.hpp"
#include <yaml-cpp/yaml.h>
#include <stdexcept>
#include <random>
#include <sstream>
#include <iomanip>
#include <cmath>


#include <regex>

static bool is_valid_uuid(const std::string& s) {
    static const std::regex UUID_RE(
        "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$",
        std::regex::icase
    );
    return std::regex_match(s, UUID_RE);
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
    if (cfg.nf_instance_id.empty()) {
        cfg.nf_instance_id = root["nf_instance_id"] ? root["nf_instance_id"].as<std::string>() : "";
    }
    if (cfg.nf_instance_id.empty() || !is_valid_uuid(cfg.nf_instance_id)) {
        throw std::runtime_error("Invalid or missing nf_instance_id in config file. A valid UUID is required.");
    }

    cfg.plmn_mcc          = n["plmn_mcc"]          ? n["plmn_mcc"].as<std::string>()          : "999";
    cfg.plmn_mnc          = n["plmn_mnc"]          ? n["plmn_mnc"].as<std::string>()          : "70";
    cfg.sbi_bind_address  = n["sbi_bind_address"]  ? n["sbi_bind_address"].as<std::string>()  : "127.0.0.1";
    cfg.sbi_port          = n["sbi_port"]          ? n["sbi_port"].as<int>()                  : 7779;

    if (n["nf_service_names"]) {
        for (const auto& kv : n["nf_service_names"])
            cfg.nf_service_names[kv.first.as<std::string>()] = kv.second.as<std::string>();
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

    cfg.nrf_uri                        = n["nrf_uri"]                        ? n["nrf_uri"].as<std::string>()               : "http://127.0.0.1:7777";
    cfg.nrf_register_on_startup        = n["nrf_register_on_startup"]        ? n["nrf_register_on_startup"].as<bool>()       : true;
    cfg.nrf_heartbeat_interval_seconds = n["nrf_heartbeat_interval_seconds"] ? n["nrf_heartbeat_interval_seconds"].as<int>() : 60;

    cfg.model_dir                = n["model_dir"]                ? n["model_dir"].as<std::string>()       : "/opt/nwdaf/models";
    cfg.anomaly_contamination    = n["anomaly_contamination"]    ? n["anomaly_contamination"].as<double>() : 0.10;
    cfg.anomaly_seed             = n["anomaly_seed"]             ? n["anomaly_seed"].as<unsigned int>()     : 0;
    cfg.anomaly_min_samples      = n["anomaly_min_samples"]      ? n["anomaly_min_samples"].as<int>()      : 10;
    cfg.baseline_stddev_min_kbps = n["baseline_stddev_min_kbps"] ? n["baseline_stddev_min_kbps"].as<double>() : 0.5;
    cfg.ewma_alpha               = n["ewma_alpha"]               ? n["ewma_alpha"].as<double>()             : 0.3;

    cfg.log_level = n["log_level"] ? n["log_level"].as<std::string>() : "info";
    cfg.log_file  = n["log_file"]  ? n["log_file"].as<std::string>()  : "/var/log/open5gs/nwdaf.log";

    // PROD-01
    cfg.history_backend = n["history_backend"] ? n["history_backend"].as<std::string>() : "none";
    cfg.history_db_path = n["history_db_path"] ? n["history_db_path"].as<std::string>() : "/opt/nwdaf/history.db";

    // PROD-06
    cfg.rate_limit_per_ip_rps = n["rate_limit_per_ip_rps"] ? n["rate_limit_per_ip_rps"].as<int>() : 10;
    cfg.rate_limit_global_rps = n["rate_limit_global_rps"] ? n["rate_limit_global_rps"].as<int>() : 100;

    // ARCH-03: configurable NETWORK_PERFORMANCE weights
    if (n["network_performance_weights"]) {
        auto w = n["network_performance_weights"];
        cfg.np_weight_nf_health = w["nf_health"] ? w["nf_health"].as<double>() : 0.6;
        cfg.np_weight_dl        = w["dl_score"]  ? w["dl_score"].as<double>()  : 0.2;
        cfg.np_weight_pdu       = w["pdu_score"] ? w["pdu_score"].as<double>() : 0.2;
    }
    // Validate that the weights sum to 1.0 (±0.001)
    double weight_sum = cfg.np_weight_nf_health + cfg.np_weight_dl + cfg.np_weight_pdu;
    if (std::abs(weight_sum - 1.0) > 0.001)
        throw std::runtime_error(
            "network_performance_weights must sum to 1.0, got " + std::to_string(weight_sum));

    // ARCH-05: TLS / mTLS configuration
    cfg.tls_enabled   = n["tls_enabled"]   ? n["tls_enabled"].as<bool>()          : false;
    cfg.tls_cert_file = n["tls_cert_file"] ? n["tls_cert_file"].as<std::string>() : "/etc/open5gs/tls/nwdaf.pem";
    cfg.tls_key_file  = n["tls_key_file"]  ? n["tls_key_file"].as<std::string>()  : "/etc/open5gs/tls/nwdaf.key";
    cfg.tls_ca_file   = n["tls_ca_file"]   ? n["tls_ca_file"].as<std::string>()   : "/etc/open5gs/tls/ca.pem";

    // P1-4: OAuth 2.0
    cfg.oauth_enabled = n["oauth_enabled"] ? n["oauth_enabled"].as<bool>()        : false;

    // H1.6: path to the OpenAPI document served from the SBI.
    cfg.openapi_spec_path = n["openapi_spec_path"]
        ? n["openapi_spec_path"].as<std::string>()
        : "/etc/open5gs/openapi/nwdaf-analytics-v1.yaml";

    return cfg;
}
