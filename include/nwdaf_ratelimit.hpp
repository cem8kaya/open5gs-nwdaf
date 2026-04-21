#pragma once
// PROD-06: Token-bucket rate limiter — one bucket per client IP plus a global bucket.
// Both TokenBucket and RateLimiter are thread-safe.
#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

class TokenBucket {
public:
    TokenBucket(int capacity, int refill_per_second)
        : capacity_(capacity), tokens_(capacity), refill_rate_(refill_per_second),
          last_refill_(std::chrono::steady_clock::now()) {}

    // Returns true and consumes a token if the bucket is non-empty.
    bool consume() {
        std::lock_guard<std::mutex> lk(mutex_);
        refill();
        if (tokens_ <= 0) return false;
        --tokens_;
        return true;
    }

private:
    void refill() {
        auto now = std::chrono::steady_clock::now();
        double elapsed_s = std::chrono::duration<double>(now - last_refill_).count();
        int added = static_cast<int>(elapsed_s * refill_rate_);
        if (added > 0) {
            tokens_ = std::min(capacity_, tokens_ + added);
            last_refill_ = now;
        }
    }

    int       capacity_;
    int       tokens_;
    int       refill_rate_;
    std::chrono::steady_clock::time_point last_refill_;
    std::mutex mutex_;
};

class RateLimiter {
public:
    RateLimiter(int per_ip_rps, int global_rps)
        : per_ip_rps_(per_ip_rps), global_rps_(global_rps),
          global_(std::make_unique<TokenBucket>(
              global_rps > 0 ? global_rps : 1,
              global_rps > 0 ? global_rps : 1)) {}

    // Returns false (and the caller should send 429) if either the global or
    // per-IP bucket is exhausted.  Returns true immediately when rps <= 0.
    bool allow(const std::string& client_ip) {
        if (per_ip_rps_ <= 0 && global_rps_ <= 0) return true;
        if (global_rps_ > 0 && !global_->consume()) return false;
        if (per_ip_rps_ <= 0) return true;
        std::lock_guard<std::mutex> lk(mutex_);
        auto& bucket = per_ip_[client_ip];
        if (!bucket)
            bucket = std::make_unique<TokenBucket>(per_ip_rps_, per_ip_rps_);
        return bucket->consume();
    }

private:
    int per_ip_rps_;
    int global_rps_;
    std::unique_ptr<TokenBucket>                              global_;
    std::unordered_map<std::string, std::unique_ptr<TokenBucket>> per_ip_;
    std::mutex mutex_;
};
