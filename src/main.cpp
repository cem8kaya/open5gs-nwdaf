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

#ifdef NWDAF_USE_SD_JOURNAL
#include <systemd/sd-daemon.h>
#endif

static std::atomic<bool> g_shutdown{false};
static NwdafServer* g_server_ptr = nullptr;
static NwdafCollector* g_collector_ptr = nullptr;

static void signalHandler(int sig) {
    spdlog::info("Received signal {}, shutting down...", sig);
    g_shutdown = true;
    if (g_server_ptr)    g_server_ptr->stop();
    if (g_collector_ptr) g_collector_ptr->stopBackgroundCollection();
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

static void registerWithNrf(const NwdafConfig& cfg) {
    httplib::Client cli(cfg.nrf_uri);
    cli.set_connection_timeout(3);
    using json = nlohmann::json;
    json body = {
        {"nfInstanceId", cfg.nf_instance_id},
        {"nfType",       "NWDAF"},
        {"nfStatus",     "REGISTERED"},
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
    auto res = cli.Put("/nnrf-nfm/v1/nf-instances/" + cfg.nf_instance_id,
                       body.dump(), "application/json");
    if (res && res->status == 201)
        spdlog::info("NRF registration successful");
    else
        spdlog::warn("NRF registration failed (status {})", res ? res->status : -1);
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
    NwdafSubscriptionStore subs;
    NwdafServer           server(engine, subs, config);

    g_server_ptr    = &server;
    g_collector_ptr = &collector;
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGINT,  signalHandler);

    collector.startBackgroundCollection();

    if (config.nrf_register_on_startup) {
        try { registerWithNrf(config); }
        catch (const std::exception& e) {
            spdlog::warn("NRF registration error: {}", e.what());
        }
    }

#ifdef NWDAF_USE_SD_JOURNAL
    sd_notify(0, "READY=1");
#endif

    spdlog::info("NWDAF ready on {}:{}", config.sbi_bind_address, config.sbi_port);
    server.start(); // blocking

#ifdef NWDAF_USE_SD_JOURNAL
    sd_notify(0, "STOPPING=1");
#endif

    spdlog::info("NWDAF stopped");
    return 0;
}
