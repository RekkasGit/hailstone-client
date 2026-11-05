#pragma once

// Work done on one background thread for an owner on another (render::ActorRenderer's new appearances), each job
// under a key (a spawn): only the latest job of a key counts. A newer job for the key supersedes the older (whose
// result is never handed back, and which is skipped if it has not started), cancel() drops the key's, and take()
// hands back each current result once, on the owner's thread. The work must not touch the owner's state: it runs
// while the owner goes on.
//
// The thread starts with the object; stop() (and the destructor) drops the jobs not started, waits for the one
// running and joins, so whatever the work uses may go once it returns.

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "platform/crash_log.h"

namespace nmsc::core {

template <class Key, class Result>
class LatestJobs {
public:
    using Work = std::function<Result()>;

    LatestJobs() : thread_([this] { run(); }) {}
    ~LatestJobs() { stop(); }
    LatestJobs(const LatestJobs&) = delete;
    LatestJobs& operator=(const LatestJobs&) = delete;

    // Queues `work` for `key`, superseding the key's job before it. Ignored after stop().
    void submit(const Key& key, Work work) {
        {
            std::lock_guard lock(mutex_);
            if (stopping_) return;
            const std::uint64_t ticket = ++next_ticket_;
            current_[key] = ticket;
            queue_.push_back({key, ticket, std::move(work)});
        }
        wake_.notify_one();
    }
    // Drops the key's job: not started, it is skipped; running or done, its result is not handed back.
    void cancel(const Key& key) {
        std::lock_guard lock(mutex_);
        current_.erase(key);
    }
    // Whether the key has a job whose result take() has not handed back yet (queued, running or done). A job whose
    // work threw is not pending: it has no result.
    bool pending(const Key& key) const {
        std::lock_guard lock(mutex_);
        return current_.contains(key);
    }
    std::size_t pending_count() const {
        std::lock_guard lock(mutex_);
        return current_.size();
    }
    // The results of the current jobs done since the last call, each with its key.
    std::vector<std::pair<Key, Result>> take() {
        std::vector<std::pair<Key, Result>> out;
        std::lock_guard lock(mutex_);
        for (auto& d : done_) {
            auto it = current_.find(d.key);
            if (it == current_.end() || it->second != d.ticket) continue; // superseded or cancelled meanwhile
            current_.erase(it);
            out.emplace_back(std::move(d.key), std::move(d.result));
        }
        done_.clear();
        return out;
    }
    // Whether stop() has begun.
    bool stopping() const {
        std::lock_guard lock(mutex_);
        return stopping_;
    }
    // Waits until no job is queued or running (tests; the owner never needs to).
    void wait_idle() {
        std::unique_lock lock(mutex_);
        idle_.wait(lock, [this] { return (queue_.empty() && !running_) || stopped_; });
    }
    // Drops the jobs not started, waits for the running one and joins the thread. Pending results are dropped too.
    void stop() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            queue_.clear();
            current_.clear();
        }
        wake_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

private:
    struct Job {
        Key key;
        std::uint64_t ticket = 0;
        Work work;
    };
    struct Done {
        Key key;
        std::uint64_t ticket = 0;
        Result result;
    };

    void run() {
        platform::CrashStack crash_stack;
        std::unique_lock lock(mutex_);
        for (;;) {
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) break;
            Job job = std::move(queue_.front());
            queue_.pop_front();
            if (auto it = current_.find(job.key); it == current_.end() || it->second != job.ticket) {
                if (queue_.empty()) idle_.notify_all();
                continue; // superseded or cancelled before it started
            }
            running_ = true;
            lock.unlock();
            bool ok = true;
            Result result{};
            try {
                result = job.work();
            } catch (...) {
                ok = false;
            }
            job.work = nullptr; // what it held goes here, on this thread, before the owner can stop()
            lock.lock();
            running_ = false;
            if (ok) {
                done_.push_back({std::move(job.key), job.ticket, std::move(result)});
            } else if (auto it = current_.find(job.key); it != current_.end() && it->second == job.ticket) {
                current_.erase(it);
            }
            if (queue_.empty()) idle_.notify_all();
        }
        stopped_ = true;
        done_.clear();
        idle_.notify_all();
    }

    mutable std::mutex mutex_; // guards everything below but thread_
    std::condition_variable wake_, idle_;
    std::deque<Job> queue_;
    std::vector<Done> done_;
    std::unordered_map<Key, std::uint64_t> current_; // key -> its latest job's ticket
    std::uint64_t next_ticket_ = 0;
    bool running_ = false;
    bool stopping_ = false;
    bool stopped_ = false;
    std::thread thread_; // last: starts after the members above are made
};

} // namespace nmsc::core
