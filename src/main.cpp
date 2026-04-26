#include "nwdaf_config.hpp"
#include "nwdaf_collector.hpp"
#include "nwdaf_analytics.hpp"
#include "nwdaf_server.hpp"
#include "nwdaf_subscription.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <csignal>
#include <atomic>
#include <string>
#include <iostream>
#include <thread>

#ifdef NWDAF_ENABLE_PUSH_DELIVERY
#include "nwdaf_notifier.hpp"
#endif

#ifdef NWDAF_USE_SD_JOURNAL
#include <systemd/sd-daemon.h>
#endif

static std::atomic<bool>   g_shutdown{false};
static NwdafServer*        g_server_ptr    = nullptr;
static NwdafCollector*     g_collector_ptr = nullptr;
static NwdafAnalyticsEngine* g_engine_ptr  = nullptr;
static std::string         g_config_path;

static void signalHandler(int sig) {
    spdlog::info("Received signal {}, shutting down...", sig);
    g_shutdown = true;
    if (g_server_ptr)    g_server_ptr->stop();
    if (g_collector_ptr) g_collector_ptr->stopBackgroundCollection();
}

// PROD-04: SIGHUP re-reads the config from disk and applies hot-reloadable
// settings (log level, collection_interval, ewma_alpha, anomaly_contamination).
// sbi_port and bind_address require a full restart.
static void sighupHandler(int) {
    spdlog::info("SIGHUP received — reloading configuration from {}", g_config_path);
    try {
        auto new_cfg = NwdafConfig::load(g_config_path);
        spdlog::info("Config reloaded: log_level={} collection_interval={}s ewma_alpha={} contamination={}",
                     new_cfg.log_level,
                     new_cfg.collection_interval_seconds,
                     new_cfg.ewma_alpha,
                     new_cfg.anomaly_contamination);

        // Apply log level
        spdlog::level::level_enum level = spdlog::level::info;
        if      (new_cfg.log_level == "trace") level = spdlog::level::trace;
        else if (new_cfg.log_level == "debug") level = spdlog::level::debug;
        else if (new_cfg.log_level == "warn")  level = spdlog::level::warn;
        else if (new_cfg.log_level == "error") level = spdlog::level::err;
        spdlog::default_logger()->set_level(level);

        if (g_collector_ptr)
            g_collector_ptr->updateConfig(new_cfg.collection_interval_seconds, new_cfg.ewma_alpha);
        if (g_engine_ptr)
            g_engine_ptr->updateConfig(new_cfg.anomaly_contamination);

    } catch (const std::exception& e) {
        spdlog::error("Config reload failed: {}", e.what());
    }
}

static void setupLogging(const NwdafConfig& cfg) {
    spdlog::level::level_enum level = spdlog::level::info;
    if      (cfg.log_level == "trace") level = spdlog::level::trace;
    else if (cfg.log_level == "debug") level = spdlog::level::debug;
    else if (cfg.log_level == "warn")  level = spdlog::level::warn;
    else if (cfg.log_level == "error") level = spdlog::level::err;

    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(level);

    std::vector<spdlog::sink_ptr> sinks{console_sink};
    try {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(cfg.log_file, true);
        file_sink->set_level(level);
        sinks.push_back(file_sink);
    } catch (...) {
        // Log file directory may not exist — console only
    }

    auto logger = std::make_shared<spdlog::logger>("nwdaf", sinks.begin(), sinks.end());
    logger->set_level(level);
    spdlog::set_default_logger(logger);
}

static int curlHttp2Request(const std::string& method, const std::string& url, const std::string& body) {
    std::string escaped_body = body;
    // Basic single quote escape for bash: replace ' with '\''
    size_t pos = 0;
    while ((pos = escaped_body.find('\'', pos)) != std::string::npos) {
        escaped_body.replace(pos, 1, "'\\''");
        pos += 4;
    }

    std::string cmd = "curl -s -o /dev/null -w \"%{http_code}\" --http2-prior-knowledge --connect-timeout 3 -X " 
                    + method + " -H \"Content-Type: application/json\" "
                    + "-H \"Accept: application/json\" "
                    + "-d '" + escaped_body + "' \"" + url + "\"";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return -1;
    char buf[16];
    if (fgets(buf, sizeof(buf), fp) != nullptr) {
        try {
            int status = std::stoi(buf);
            pclose(fp);
            return status;
        } catch (...) {}
    }
    pclose(fp);
    return -1;
}

static void registerWithNrf(const NwdafConfig& cfg) {
    using json = nlohmann::json;
    json body = {
        {"nfInstanceId", cfg.nf_instance_id},
        {"nfType",       "NWDAF"},
        {"nfStatus",     "REGISTERED"},
        {"ipv4Addresses", {cfg.sbi_bind_address}},
        {"plmnList",     {{{"mcc", cfg.plmn_mcc}, {"mnc", cfg.plmn_mnc}}}},
        {"nfServices",   {{
            {"serviceInstanceId", "nwdaf-analytics-1"},
            {"serviceName",       "nnwdaf-analyticsinfo"},
            {"versions",          {{{"apiVersionInUri", "v1"}, {"apiFullVersion", "1.0.0"}}}},
            {"scheme",            "http"},
            {"nfServiceStatus",   "REGISTERED"},
            {"ipEndPoints",       {{{"ipv4Address", cfg.sbi_bind_address}, {"port", cfg.sbi_port}}}}
        }}}
    };
    std::string url = cfg.nrf_uri + "/nnrf-nfm/v1/nf-instances/" + cfg.nf_instance_id;
    int status = curlHttp2Request("PUT", url, body.dump());
    
    if (status == 201 || status == 200)
        spdlog::info("NRF registration successful ({} {})", status, status == 201 ? "Created" : "OK");
    else
        spdlog::warn("NRF registration failed (status {})", status);
}

int main(int argc, char* argv[]) {
    std::string config_path = "/etc/open5gs/nwdaf.yaml";
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == "--config") config_path = argv[i + 1];
    }

    NwdafConfig config;
    try {
        config = NwdafConfig::load(config_path);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << "\n";
        return 1;
    }

    setupLogging(config);
    spdlog::info("Open5GS NWDAF starting (instance: {})", config.nf_instance_id);

    NwdafCollector        collector(config);
    NwdafAnalyticsEngine  engine(collector, config);

    // PROD-02: wire up persistent subscription backend when SQLite is available
    std::shared_ptr<NwdafSubscriptionBackend> sub_backend;
#ifdef NWDAF_HAS_SQLITE
    if (config.history_backend == "sqlite") {
        sub_backend = std::make_shared<SqliteSubscriptionBackend>(config.history_db_path);
    }
#endif
    NwdafSubscriptionStore subs(sub_backend);
    NwdafServer           server(engine, subs, config);

    g_server_ptr    = &server;
    g_collector_ptr = &collector;
    g_engine_ptr    = &engine;
    g_config_path   = config_path;
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGHUP,  sighupHandler);  // PROD-04

    collector.startBackgroundCollection();

    if (config.nrf_register_on_startup) {
        try { registerWithNrf(config); }
        catch (const std::exception& e) {
            spdlog::warn("NRF registration error: {}", e.what());
        }
    }

    // COMP-01: subscription push delivery notifier
    // PROD-03: pass server's atomic counters so /metrics can expose them
#ifdef NWDAF_ENABLE_PUSH_DELIVERY
    NwdafNotifier notifier(subs, engine, 5,
                           &server.notif_total_, &server.notif_failures_);
    notifier.start();
#endif

    // COMP-02: NRF heartbeat thread (TS 29.510 §5.3.2.4)
    std::thread nrf_hb_thread;
    if (config.nrf_register_on_startup && config.nrf_heartbeat_interval_seconds > 0) {
        nrf_hb_thread = std::thread([&config]() {
            int elapsed = 0;
            while (!g_shutdown) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                if (g_shutdown) break;
                if (++elapsed < config.nrf_heartbeat_interval_seconds) continue;
                elapsed = 0;
                try {
                    nlohmann::json patch = {{"nfStatus", "REGISTERED"}};
                    std::string url = config.nrf_uri + "/nnrf-nfm/v1/nf-instances/" + config.nf_instance_id;
                    int status = curlHttp2Request("PATCH", url, patch.dump());
                    if (status != 200 && status != 204) {
                        spdlog::warn("NRF heartbeat failed ({}), re-registering", status);
                        registerWithNrf(config);
                    } else {
                        spdlog::info("NRF heartbeat sent successfully ({} OK)", status);
                    }
                } catch (const std::exception& e) {
                    spdlog::warn("NRF heartbeat exception: {}", e.what());
                }
            }
        });
    }

#ifdef NWDAF_USE_SD_JOURNAL
    sd_notify(0, "READY=1");
#endif

    spdlog::info("NWDAF ready on {}:{}", config.sbi_bind_address, config.sbi_port);
    server.start(); // blocking

#ifdef NWDAF_ENABLE_PUSH_DELIVERY
    notifier.stop();
#endif

    if (nrf_hb_thread.joinable()) nrf_hb_thread.join();

#ifdef NWDAF_USE_SD_JOURNAL
    sd_notify(0, "STOPPING=1");
#endif

    spdlog::info("NWDAF stopped");
    return 0;
}
