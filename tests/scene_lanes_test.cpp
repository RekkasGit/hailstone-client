// render::SceneLanes: the scene recorder's lanes (render/scene_recorder.h), recorded on the workers.
#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <mutex>
#include <thread>
#include <vector>

#include "core/worker_pool.h"
#include "render/scene_lanes.h"

using namespace hailstone;

TEST_CASE("Scene lanes: each lane's jobs record in order on one thread, and the jobs keep the order they came in") {
    const std::array<int, 4> owners{};
    // Jobs from four renderers, interleaved as a frame queues them.
    const std::vector<std::size_t> order{0, 1, 0, 2, 1, 0, 3, 3, 2, 0, 1, 1, 0};
    for (unsigned workers : {0u, 1u, 3u}) {
        CAPTURE(workers);
        core::WorkerPool pool(workers);
        core::set_workers(&pool);
        render::SceneLanes lanes;
        for (int frame = 0; frame < 50; ++frame) {
            lanes.clear();
            for (std::size_t owner : order) lanes.add(&owners[owner]);
            REQUIRE(lanes.jobs() == order.size());
            REQUIRE(lanes.lanes() == 4);
            for (std::size_t j = 0; j < order.size(); ++j) REQUIRE(lanes.lane_of(j) == order[j]); // by first use
            // A fake command buffer per job: what its lane had recorded before it, and the thread that recorded it.
            struct Recorded {
                int calls = 0, before = -1;
                std::thread::id thread;
            };
            std::vector<Recorded> buffers(order.size());
            std::array<int, 4> recorded{};
            std::mutex mutex;
            lanes.record([&](std::size_t j) {
                const std::size_t lane = lanes.lane_of(j);
                std::lock_guard lock(mutex);
                ++buffers[j].calls;
                buffers[j].before = recorded[lane]++;
                buffers[j].thread = std::this_thread::get_id();
            });
            // The buffers executed in the jobs' order: each once, each lane's in order, on its lane's one thread.
            std::array<int, 4> seen{};
            std::array<std::thread::id, 4> thread{};
            for (std::size_t j = 0; j < order.size(); ++j) {
                const std::size_t lane = order[j];
                REQUIRE(buffers[j].calls == 1);
                CHECK(buffers[j].before == seen[lane]++);
                if (thread[lane] == std::thread::id{}) thread[lane] = buffers[j].thread;
                CHECK(buffers[j].thread == thread[lane]);
            }
        }
        core::set_workers(nullptr);
    }
}

TEST_CASE("Scene lanes: cleared, a frame starts with no lanes") {
    int a = 0, b = 0;
    render::SceneLanes lanes;
    CHECK(lanes.add(&a) == 0);
    CHECK(lanes.add(&b) == 1);
    lanes.clear();
    CHECK(lanes.jobs() == 0);
    CHECK(lanes.lanes() == 0);
    CHECK(lanes.add(&b) == 0);
    int calls = 0;
    lanes.record([&](std::size_t j) {
        CHECK(j == 0);
        ++calls;
    });
    CHECK(calls == 1);
    lanes.clear();
    lanes.record([&](std::size_t) { ++calls; });
    CHECK(calls == 1);
}
