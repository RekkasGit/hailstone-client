#pragma once

// Which character textures are read already or being read (render::ActorRenderer's appearance jobs and its main
// thread), so each is decoded once, on the worker where it can be.
//
// A job claims a key before it reads the texture; the claim holds while its result waits to be installed. A result
// that is dropped (the spawn changed again or left, the job was superseded) gives its claims back through its
// ClaimHold, so the next job that needs the texture reads it rather than the main thread. A key whose texture has a
// slot is installed: claimed until eviction. A key claimed but not installed is pending: some job is reading it or holds it
// read, and an appearance that needs it can wait a frame for it (ActorRenderer::prepare_spawns).

#include <memory>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hailstone::render {

// Shared by the worker and the main thread.
class TextureClaims {
public:
    // False: claimed before (by a job, or installed).
    bool claim(const std::string& key) {
        return acquire(key).has_value();
    }
    std::optional<std::uint64_t> acquire(const std::string& key) {
        std::lock_guard lock(mutex_);
        if (keys_.contains(key)) return std::nullopt;
        const auto generation = ++generation_;
        keys_.emplace(key, generation);
        return generation;
    }
    // A claim given up before its texture was installed: the next claim gets it. An installed key stays.
    void release(const std::string& key, std::uint64_t generation) {
        std::lock_guard lock(mutex_);
        auto it = keys_.find(key);
        if (it != keys_.end() && it->second == generation && !installed_.contains(key)) keys_.erase(it);
    }
    // The texture has its slot.
    void installed(const std::string& key) {
        std::lock_guard lock(mutex_);
        if (!keys_.contains(key)) keys_.emplace(key, ++generation_);
        installed_.insert(key);
    }
    // The main thread retired the slot. A later appearance must decode it again.
    // Generations keep an old result's later destruction from giving back a
    // replacement job's claim for the same key.
    void evicted(const std::string& key) {
        std::lock_guard lock(mutex_);
        installed_.erase(key);
        keys_.erase(key);
    }
    // Claimed by a job whose texture is not installed yet.
    bool pending(const std::string& key) const {
        std::lock_guard lock(mutex_);
        return keys_.contains(key) && !installed_.contains(key);
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::uint64_t> keys_;
    std::unordered_set<std::string> installed_;
    std::uint64_t generation_ = 0;
};

// The keys one job claimed, given back when the job's result goes (whatever thread drops it); keys installed by
// then stay claimed.
class ClaimHold {
public:
    ClaimHold() = default;
    explicit ClaimHold(std::shared_ptr<TextureClaims> claims) : claims_(std::move(claims)) {}
    ClaimHold(ClaimHold&& other) noexcept
        : claims_(std::move(other.claims_)), keys_(std::move(other.keys_)) {
        other.keys_.clear();
    }
    ClaimHold& operator=(ClaimHold&& other) noexcept {
        if (this != &other) {
            give_back();
            claims_ = std::move(other.claims_);
            keys_ = std::move(other.keys_);
            other.keys_.clear();
        }
        return *this;
    }
    ClaimHold(const ClaimHold&) = delete;
    ClaimHold& operator=(const ClaimHold&) = delete;
    ~ClaimHold() { give_back(); }

    // Claims `key` for this job: false when it was claimed before (the job does not read it then).
    bool claim(const std::string& key) {
        if (!claims_) return false;
        const auto generation = claims_->acquire(key);
        if (!generation) return false;
        keys_.emplace_back(key, *generation);
        return true;
    }

private:
    void give_back() {
        if (claims_)
            for (const auto& [key, generation] : keys_) claims_->release(key, generation);
        keys_.clear();
    }

    std::shared_ptr<TextureClaims> claims_;
    std::vector<std::pair<std::string, std::uint64_t>> keys_;
};

} // namespace hailstone::render
