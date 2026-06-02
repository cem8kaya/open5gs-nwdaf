#include "nwdaf_subscription.hpp"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

#ifdef NWDAF_HAS_SQLITE
#include <sqlite3.h>
#endif

static std::string generateSubId() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    std::ostringstream oss;
    oss << "sub-" << std::hex << std::setw(16) << std::setfill('0') << dis(gen);
    return oss.str();
}

static std::string nowISO() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    gmtime_r(&t, &tm_buf);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return buf;
}

// ── PROD-02: NwdafSubscriptionStore ──────────────────────────────────────────

NwdafSubscriptionStore::NwdafSubscriptionStore(
    std::shared_ptr<NwdafSubscriptionBackend> backend)
    : backend_(std::move(backend))
{
    if (backend_) {
        for (auto& sub : backend_->loadAll())
            store_[sub.sub_id] = sub;
        spdlog::info("Loaded {} subscriptions from persistent backend", store_.size());
    }
}

std::string NwdafSubscriptionStore::create(const json& body) {
    Subscription sub;
    sub.sub_id        = generateSubId();
    sub.analytics_id  = body.value("analyticsId", "");
    sub.notif_uri     = body.value("notifUri", "");
    sub.notif_id      = body.value("notifId", "");
    sub.rep_period_seconds = body.value("repPeriod", 60);
    sub.max_report_nbr     = body.value("maxReportNbr", 0);
    sub.created_at_iso     = nowISO();
    sub.status             = "ACTIVE";

    {
        std::lock_guard<std::mutex> lk(mutex_);
        store_[sub.sub_id] = sub;
    }
    if (backend_) backend_->persist(sub);
    return sub.sub_id;
}

bool NwdafSubscriptionStore::exists(const std::string& sub_id) const {
    std::lock_guard<std::mutex> lk(mutex_);
    return store_.count(sub_id) > 0;
}

Subscription NwdafSubscriptionStore::get(const std::string& sub_id) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = store_.find(sub_id);
    if (it == store_.end()) throw std::out_of_range("Subscription not found: " + sub_id);
    return it->second;
}

bool NwdafSubscriptionStore::remove(const std::string& sub_id) {
    bool erased;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        erased = store_.erase(sub_id) > 0;
    }
    if (erased && backend_) backend_->remove(sub_id);
    return erased;
}

std::vector<Subscription> NwdafSubscriptionStore::listAll() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<Subscription> out;
    out.reserve(store_.size());
    for (const auto& [k, v] : store_) out.push_back(v);
    return out;
}

int NwdafSubscriptionStore::count() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return (int)store_.size();
}

int NwdafSubscriptionStore::incrementReportCount(const std::string& sub_id) {
    Subscription* sub_ptr = nullptr;
    int new_count;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = store_.find(sub_id);
        if (it == store_.end()) return -1;
        new_count = ++it->second.report_count;
        sub_ptr = &it->second;
        if (backend_) backend_->persist(*sub_ptr);
    }
    return new_count;
}

// ── PROD-02: SqliteSubscriptionBackend ───────────────────────────────────────

#ifdef NWDAF_HAS_SQLITE

static const char* SUBS_DDL =
    "CREATE TABLE IF NOT EXISTS nwdaf_subscriptions ("
    "  sub_id TEXT PRIMARY KEY,"
    "  analytics_id TEXT, notif_uri TEXT, notif_id TEXT,"
    "  rep_period INTEGER, max_report_nbr INTEGER, report_count INTEGER,"
    "  created_at TEXT, status TEXT"
    ");";

SqliteSubscriptionBackend::SqliteSubscriptionBackend(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        spdlog::warn("PROD-02: Cannot open subscription DB {}: {}",
                     db_path, sqlite3_errmsg(db_));
        sqlite3_close(db_);
        db_ = nullptr;
        return;
    }
    char* err = nullptr;
    if (sqlite3_exec(db_, SUBS_DDL, nullptr, nullptr, &err) != SQLITE_OK) {
        spdlog::warn("PROD-02: Subscription DB DDL error: {}", err);
        sqlite3_free(err);
    }
    spdlog::info("PROD-02: subscription DB opened at {}", db_path);
}

SqliteSubscriptionBackend::~SqliteSubscriptionBackend() {
    if (db_) { sqlite3_close(db_); db_ = nullptr; }
}

void SqliteSubscriptionBackend::persist(const Subscription& sub) {
    if (!db_) return;
    const char* sql =
        "INSERT OR REPLACE INTO nwdaf_subscriptions"
        " (sub_id, analytics_id, notif_uri, notif_id,"
        "  rep_period, max_report_nbr, report_count, created_at, status)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, sub.sub_id.c_str(),        -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sub.analytics_id.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sub.notif_uri.c_str(),     -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, sub.notif_id.c_str(),      -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt,  5, sub.rep_period_seconds);
    sqlite3_bind_int(stmt,  6, sub.max_report_nbr);
    sqlite3_bind_int(stmt,  7, sub.report_count);
    sqlite3_bind_text(stmt, 8, sub.created_at_iso.c_str(),-1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, sub.status.c_str(),        -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

bool SqliteSubscriptionBackend::remove(const std::string& sub_id) {
    if (!db_) return false;
    const char* sql = "DELETE FROM nwdaf_subscriptions WHERE sub_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, sub_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return true;
}

std::vector<Subscription> SqliteSubscriptionBackend::loadAll() {
    std::vector<Subscription> out;
    if (!db_) return out;
    const char* sql =
        "SELECT sub_id, analytics_id, notif_uri, notif_id,"
        "       rep_period, max_report_nbr, report_count, created_at, status"
        " FROM nwdaf_subscriptions;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Subscription s;
        auto col = [&](int i) -> std::string {
            const char* v = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            return v ? v : "";
        };
        s.sub_id            = col(0);
        s.analytics_id      = col(1);
        s.notif_uri         = col(2);
        s.notif_id          = col(3);
        s.rep_period_seconds= sqlite3_column_int(stmt, 4);
        s.max_report_nbr    = sqlite3_column_int(stmt, 5);
        s.report_count      = sqlite3_column_int(stmt, 6);
        s.created_at_iso    = col(7);
        s.status            = col(8);
        out.push_back(std::move(s));
    }
    sqlite3_finalize(stmt);
    return out;
}

#endif  // NWDAF_HAS_SQLITE
