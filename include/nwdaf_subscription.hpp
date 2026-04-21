#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct Subscription {
    std::string sub_id;
    std::string analytics_id;
    std::string notif_uri;
    std::string notif_id;
    int         rep_period_seconds;
    int         max_report_nbr;
    int         report_count = 0;   // incremented by NwdafNotifier on each successful delivery
    std::string created_at_iso;
    std::string status;
};

// PROD-02: pluggable persistence backend for NwdafSubscriptionStore.
// NullSubscriptionBackend (default) gives the original in-memory-only behaviour.
class NwdafSubscriptionBackend {
public:
    virtual ~NwdafSubscriptionBackend() = default;
    virtual void persist(const Subscription& sub) = 0;
    virtual bool remove(const std::string& sub_id) = 0;
    virtual std::vector<Subscription> loadAll() = 0;
};

class NullSubscriptionBackend : public NwdafSubscriptionBackend {
public:
    void persist(const Subscription&) override {}
    bool remove(const std::string&) override { return true; }
    std::vector<Subscription> loadAll() override { return {}; }
};

#ifdef NWDAF_HAS_SQLITE
// Forward-declared to avoid pulling <sqlite3.h> into this header.
struct sqlite3;

class SqliteSubscriptionBackend : public NwdafSubscriptionBackend {
public:
    explicit SqliteSubscriptionBackend(const std::string& db_path);
    ~SqliteSubscriptionBackend();
    void persist(const Subscription& sub) override;
    bool remove(const std::string& sub_id) override;
    std::vector<Subscription> loadAll() override;
private:
    sqlite3* db_ = nullptr;
};
#endif

class NwdafSubscriptionStore {
public:
    // PROD-02: optional backend; nullptr → in-memory only (original behaviour)
    explicit NwdafSubscriptionStore(
        std::shared_ptr<NwdafSubscriptionBackend> backend = nullptr);

    std::string               create(const json& body);
    bool                      exists(const std::string& sub_id) const;
    Subscription              get(const std::string& sub_id) const;
    bool                      remove(const std::string& sub_id);
    std::vector<Subscription> listAll() const;
    int                       count() const;
    // Atomically increment report_count; returns new count, or -1 if not found
    int                       incrementReportCount(const std::string& sub_id);

private:
    std::shared_ptr<NwdafSubscriptionBackend>     backend_;
    mutable std::mutex                            mutex_;
    std::unordered_map<std::string, Subscription> store_;
};
