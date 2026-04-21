#include "nwdaf_notifier.hpp"
#include <spdlog/spdlog.h>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>

using json = nlohmann::json;

// Split "http://host:port/path/..." → {"http://host:port", "/path/..."}
static std::pair<std::string, std::string> splitUrl(const std::string& url) {
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return {url, "/"};
    auto path_start = url.find('/', scheme_end + 3);
    if (path_start == std::string::npos) return {url, "/"};
    return {url.substr(0, path_start), url.substr(path_start)};
}

static std::string nowISO() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    gmtime_r(&t, &tm_buf);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return buf;
}

NwdafNotifier::NwdafNotifier(NwdafSubscriptionStore& subs,
                              NwdafAnalyticsEngine&   engine,
                              int poll_interval_seconds,
                              std::atomic<uint64_t>*  notif_total,
                              std::atomic<uint64_t>*  notif_failures)
    : subs_(subs), engine_(engine), poll_interval_s_(poll_interval_seconds),
      notif_total_(notif_total), notif_failures_(notif_failures)
{}

void NwdafNotifier::start() {
    if (running_) return;
    running_ = true;
    thread_ = std::thread(&NwdafNotifier::deliveryLoop, this);
    spdlog::info("NwdafNotifier started (poll interval {}s)", poll_interval_s_);
}

void NwdafNotifier::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    spdlog::info("NwdafNotifier stopped");
}

void NwdafNotifier::deliveryLoop() {
    while (running_) {
        // Sleep in 1-second increments for clean shutdown
        for (int i = 0; i < poll_interval_s_ && running_; ++i)
            std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!running_) break;

        auto subs = subs_.listAll();
        auto now  = std::chrono::steady_clock::now();

        for (const auto& sub : subs) {
            if (sub.status != "ACTIVE") continue;
            if (sub.notif_uri.empty())  continue;

            std::chrono::steady_clock::time_point last;
            {
                std::lock_guard<std::mutex> lk(ts_mutex_);
                auto it = last_delivered_.find(sub.sub_id);
                if (it != last_delivered_.end()) last = it->second;
            }

            auto elapsed_s = std::chrono::duration_cast<std::chrono::seconds>(
                now - last).count();
            if (last != std::chrono::steady_clock::time_point{} &&
                elapsed_s < sub.rep_period_seconds)
                continue;  // not due yet

            deliver(sub);
        }
    }
}

void NwdafNotifier::deliver(const Subscription& sub) {
    json anal_data;
    try {
        anal_data = engine_.compute(sub.analytics_id);
    } catch (const std::exception& e) {
        spdlog::warn("Notifier: compute failed for sub {} ({}): {}",
                     sub.sub_id, sub.analytics_id, e.what());
        return;
    }

    json body = {
        {"subId",       sub.sub_id},
        {"notifId",     sub.notif_id.empty() ? sub.sub_id : sub.notif_id},
        {"analyticsId", sub.analytics_id},
        {"ts",          nowISO()},
        {"analData",    anal_data}
    };

    auto [base, path] = splitUrl(sub.notif_uri);
    try {
        httplib::Client cli(base);
        cli.set_connection_timeout(3);
        cli.set_read_timeout(5);
        auto res = cli.Post(path, body.dump(), "application/json");

        if (res && res->status >= 200 && res->status < 300) {
            // PROD-03: increment shared notification counter
            if (notif_total_) ++(*notif_total_);

            // Update last-delivered timestamp
            {
                std::lock_guard<std::mutex> lk(ts_mutex_);
                last_delivered_[sub.sub_id] = std::chrono::steady_clock::now();
            }

            int new_count = subs_.incrementReportCount(sub.sub_id);
            spdlog::debug("Notifier: delivered {} to {} (count={})",
                          sub.analytics_id, sub.notif_uri, new_count);

            // Auto-delete when max_report_nbr reached
            if (sub.max_report_nbr > 0 && new_count >= sub.max_report_nbr) {
                subs_.remove(sub.sub_id);
                spdlog::info("Notifier: auto-deleted sub {} after {} reports",
                             sub.sub_id, new_count);
            }
        } else {
            // PROD-03: increment shared failure counter
            if (notif_failures_) ++(*notif_failures_);
            spdlog::warn("Notifier: delivery failed for sub {} → {} (HTTP {})",
                         sub.sub_id, sub.notif_uri,
                         res ? std::to_string(res->status) : "no response");
        }
    } catch (const std::exception& e) {
        if (notif_failures_) ++(*notif_failures_);
        spdlog::warn("Notifier: delivery exception for sub {}: {}", sub.sub_id, e.what());
    }
}
