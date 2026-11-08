// core::LatestJobs: background work keyed by spawn, where only a key's latest job counts.
#include <doctest/doctest.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include "core/latest_jobs.h"

using hailstone::core::LatestJobs;

namespace {

// Holds the worker inside a job until released, so a test can act while it runs.
struct Gate {
    std::mutex m;
    std::condition_variable cv;
    bool entered = false, open = false;
    void pass() {
        std::unique_lock lock(m);
        entered = true;
        cv.notify_all();
        cv.wait(lock, [this] { return open; });
    }
    void wait_entered() {
        std::unique_lock lock(m);
        cv.wait(lock, [this] { return entered; });
    }
    void release() {
        std::lock_guard lock(m);
        open = true;
        cv.notify_all();
    }
};

} // namespace

TEST_CASE("latest jobs: results come back on take(), each once") {
    LatestJobs<int, std::string> jobs;
    jobs.submit(1, [] { return std::string("one"); });
    jobs.submit(2, [] { return std::string("two"); });
    CHECK(jobs.pending(1));
    jobs.wait_idle();
    auto out = jobs.take();
    REQUIRE(out.size() == 2);
    CHECK(out[0] == std::pair{1, std::string("one")});
    CHECK(out[1] == std::pair{2, std::string("two")});
    CHECK_FALSE(jobs.pending(1));
    CHECK(jobs.take().empty());
}

TEST_CASE("latest jobs: a key's newer job supersedes the one before (the appearance changed again)") {
    LatestJobs<int, int> jobs;
    Gate gate;
    std::atomic<int> runs{0};
    jobs.submit(1, [&] { gate.pass(); return 10; });
    gate.wait_entered(); // running: its result must not come back
    jobs.submit(1, [&] { ++runs; return 11; });
    jobs.submit(1, [&] { ++runs; return 12; }); // the 11 is superseded before it starts: skipped
    gate.release();
    jobs.wait_idle();
    auto out = jobs.take();
    REQUIRE(out.size() == 1);
    CHECK(out[0].second == 12);
    CHECK(runs == 1);
}

TEST_CASE("latest jobs: a cancelled key's result is dropped (the spawn left)") {
    LatestJobs<int, int> jobs;
    Gate gate;
    jobs.submit(1, [&] { gate.pass(); return 1; });
    jobs.submit(2, [] { return 2; });
    gate.wait_entered();
    jobs.cancel(1); // running
    jobs.cancel(2); // not started
    CHECK_FALSE(jobs.pending(1));
    gate.release();
    jobs.wait_idle();
    CHECK(jobs.take().empty());

    // Done but not taken yet, then cancelled: still dropped.
    jobs.submit(3, [] { return 3; });
    jobs.wait_idle();
    jobs.cancel(3);
    CHECK(jobs.take().empty());
    // A key used again after its cancel gets its new result.
    jobs.submit(3, [] { return 4; });
    jobs.wait_idle();
    auto out = jobs.take();
    REQUIRE(out.size() == 1);
    CHECK(out[0].second == 4);
}

TEST_CASE("latest jobs: a job that throws gives no result and is no longer pending") {
    LatestJobs<int, int> jobs;
    jobs.submit(1, []() -> int { throw std::runtime_error("bad archive"); });
    jobs.submit(2, [] { return 2; });
    jobs.wait_idle();
    CHECK_FALSE(jobs.pending(1));
    auto out = jobs.take();
    REQUIRE(out.size() == 1);
    CHECK(out[0].first == 2);
}

TEST_CASE("latest jobs: stop() waits for the running job, drops the rest and joins") {
    auto alive = std::make_shared<int>(7);
    std::weak_ptr<int> watch = alive;
    Gate gate;
    std::atomic<int> later{0};
    auto jobs = std::make_unique<LatestJobs<int, int>>();
    jobs->submit(1, [&gate, held = alive] { gate.pass(); return *held; });
    jobs->submit(2, [&later] { return ++later; });
    alive.reset(); // only the job holds it now
    gate.wait_entered();
    std::thread opener([&] {
        while (!jobs->stopping()) std::this_thread::yield();
        gate.release();
    });
    jobs->stop(); // returns only once the running job is done: what it held is released by then
    opener.join();
    CHECK(watch.expired());
    CHECK(later == 0);
    CHECK(jobs->take().empty());
    jobs->submit(3, [] { return 3; }); // ignored after stop
    CHECK_FALSE(jobs->pending(3));
    jobs->wait_idle();
    jobs.reset(); // stop() again in the destructor: harmless
}
