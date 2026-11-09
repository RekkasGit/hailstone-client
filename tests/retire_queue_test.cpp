// render/retire_queue.h: when what the CPU let go of may be destroyed (VkContext::retire, the uploads' staging
// buffers), and the uploads in flight counted done in order.
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "render/retire_queue.h"

using namespace hailstone::render;

TEST_CASE("Retire queue: a release runs once its frame and its upload are done, oldest first among those ready") {
    RetireQueue q;
    std::vector<int> ran;
    q.push({3, 0}, [&] { ran.push_back(1); });  // used by frame 3
    q.push({0, 2}, [&] { ran.push_back(2); });  // staging of upload 2
    q.push({3, 5}, [&] { ran.push_back(3); });  // frame 3 and upload 5
    q.push({4, 1}, [&] { ran.push_back(4); });
    CHECK(q.collect({0, 1}) == 0);  // upload 2 not done, frame 3 not done
    CHECK(q.collect({2, 10}) == 1); // the staging only: frame 3 not done
    CHECK(ran == std::vector<int>{2});
    CHECK(q.collect({3, 2}) == 1);  // frame 3 done
    CHECK(ran == std::vector<int>{2, 1});
    CHECK(q.size() == 2);
    CHECK(q.collect({3, 4}) == 0);
    CHECK(q.collect({4, 5}) == 2);
    CHECK(ran == std::vector<int>{2, 1, 3, 4});
    CHECK(q.empty());
}

TEST_CASE("Retire queue: flush runs everything, a release that retires more included") {
    RetireQueue q;
    int ran = 0;
    q.push({100, 100}, [&] {
        ++ran;
        q.push({200, 200}, [&] { ++ran; });
    });
    CHECK(q.collect({1, 1}) == 0);
    CHECK(q.flush() == 2);
    CHECK(ran == 2);
    CHECK(q.empty());
}

TEST_CASE("Retire queue: a release pushed from a collect waits for the next one") {
    RetireQueue q;
    int ran = 0;
    q.push({1, 0}, [&] {
        ++ran;
        q.push({1, 0}, [&] { ++ran; });
    });
    CHECK(q.collect({1, 0}) == 1);
    CHECK(ran == 1);
    CHECK(q.collect({1, 0}) == 1);
    CHECK(ran == 2);
}

TEST_CASE("Upload tracker: serials from 1, done in order, the staging bytes in flight") {
    UploadTracker<int> t;
    std::vector<int> recycled;
    auto recycle = [&](int h) { recycled.push_back(h); };
    CHECK(t.completed() == 0);
    CHECK(t.submitted(10, 100) == 1);
    CHECK(t.submitted(20, 50) == 2);
    CHECK(t.submitted(30, 25) == 3);
    CHECK(t.last() == 3);
    CHECK(t.completed() == 0);
    CHECK(t.bytes_in_flight() == 175);
    // The second finished before the first: not counted until the first is.
    t.poll([](int h) { return h == 20; }, recycle);
    CHECK(t.completed() == 0);
    CHECK(recycled.empty());
    t.poll([](int h) { return h <= 20; }, recycle);
    CHECK(t.completed() == 2);
    CHECK(recycled == std::vector<int>{10, 20});
    CHECK(t.bytes_in_flight() == 25);
    // Waiting for the oldest.
    int waited = 0;
    CHECK(t.wait_oldest([&](int h) { waited = h; }, recycle));
    CHECK(waited == 30);
    CHECK(t.completed() == 3);
    CHECK_FALSE(t.wait_oldest([&](int) {}, recycle));
    // The device waited for: every one done.
    t.submitted(40, 1);
    t.submitted(50, 1);
    t.all_done(recycle);
    CHECK(t.completed() == 5);
    CHECK(t.pending() == 0);
    CHECK(t.bytes_in_flight() == 0);
    CHECK(recycled == std::vector<int>{10, 20, 30, 40, 50});
}

TEST_CASE("Retire queue: an upload's staging goes with its fence, a frame's resources with the frame") {
    // The way VkContext uses the two: staging retired at {0, serial}, a renderer's objects at {frames_begun, last}.
    RetireQueue q;
    UploadTracker<int> uploads;
    std::vector<std::string> ran;
    auto noop = [](int) {};
    const std::uint64_t first = uploads.submitted(1, 64);
    q.push({0, first}, [&] { ran.push_back("staging"); });
    q.push({7, uploads.last()}, [&] { ran.push_back("zone"); }); // freed while frame 7 was recorded
    uploads.poll([](int) { return true; }, noop);
    CHECK(q.collect({frames_done_at(8, 2), uploads.completed()}) == 1); // frame 8 begun: frame 6 done
    CHECK(ran == std::vector<std::string>{"staging"});
    CHECK(q.collect({frames_done_at(9, 2), uploads.completed()}) == 1); // frame 9 begun: frame 7 done
    CHECK(ran == std::vector<std::string>{"staging", "zone"});
}

TEST_CASE("Frames done: the frame kFramesInFlight back once a frame waited for its slot") {
    CHECK(frames_done_at(0, 2) == 0);
    CHECK(frames_done_at(1, 2) == 0);
    CHECK(frames_done_at(2, 2) == 0);
    CHECK(frames_done_at(3, 2) == 1);
    CHECK(frames_done_at(10, 2) == 8);
    CHECK(gpu_point_reached({0, 0}, {0, 0}));
    CHECK_FALSE(gpu_point_reached({1, 0}, {0, 5}));
}
