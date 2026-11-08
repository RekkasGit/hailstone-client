// render/look_eviction.h: when an idle dressed appearance may be freed.

#include <doctest/doctest.h>

#include <map>
#include <memory>

#include "render/look_eviction.h"

using hailstone::render::kLookIdleMs;
using hailstone::render::kLookSweepMs;
using hailstone::render::look_idle;

TEST_CASE("look_idle: idle long enough and past the frames in flight") {
    CHECK_FALSE(look_idle(100, 1000, 100, 1000, 2));               // drawn this frame
    CHECK_FALSE(look_idle(100, 1000, 5000, 1000 + kLookIdleMs - 1, 2)); // not idle long enough
    CHECK(look_idle(100, 1000, 5000, 1000 + kLookIdleMs, 2));
    // However long ago by the clock, a frame still on the GPU may read its buffers.
    CHECK_FALSE(look_idle(100, 1000, 101, 1000 + 2 * kLookIdleMs, 2));
    CHECK(look_idle(100, 1000, 102, 1000 + 2 * kLookIdleMs, 2));
    // The display clock wrapping past 2^32 ms.
    CHECK(look_idle(1, 0xffffff00u, 10, kLookIdleMs, 2));
    CHECK_FALSE(look_idle(1, 0xffffff00u, 10, 0x10u, 2));
}

namespace {
struct FakeMesh {};
struct FakeModel {};
// ActorRenderer::SpawnState's fields the rules read.
struct FakeState {
    std::shared_ptr<const FakeModel> model;
    std::shared_ptr<const FakeMesh> mesh;
    bool seen = false;
    bool hidden = false;
};
} // namespace

TEST_CASE("A spawn hidden on purpose keeps the look it wears past kLookIdleMs; drawn again it starts afresh") {
    using namespace hailstone::render;
    auto model = std::make_shared<const FakeModel>();
    auto worn_look = std::make_shared<const FakeMesh>();
    auto idle_look = std::make_shared<const FakeMesh>(); // dressed once, no spawn wears it
    std::map<std::uint32_t, FakeState> states;
    states[1] = FakeState{model, worn_look, true, false};
    const std::uint32_t drawn_ms = 1000; // both looks last drawn here
    int drops = 0;
    // Frames in which spawn 1 is hidden (prepare_spawns' order: unseen states erased, then the looks swept).
    for (std::uint32_t now = drawn_ms + kLookSweepMs; now <= drawn_ms + 2 * kLookIdleMs; now += kLookSweepMs) {
        for (auto& kv : states) kv.second.seen = false;
        if (auto it = states.find(1); it != states.end() && hide_spawn_state(it->second)) ++drops;
        std::erase_if(states, [](const auto& kv) { return !kv.second.seen; });
        REQUIRE(states.contains(1));
        const auto worn = worn_meshes(states);
        const bool idle = look_idle(1, drawn_ms, 1000, now, 2);
        CHECK(worn.contains(worn_look.get())); // never freed while hidden
        CHECK_FALSE(worn.contains(idle_look.get()));
        if (now >= drawn_ms + kLookIdleMs) CHECK(idle); // so the look no state wears would go
    }
    CHECK(drops == 1); // its pending dressing is dropped once, when it is first hidden
    CHECK(states.at(1).hidden);
    // Drawn again with the same model: a new state, which then finds the kept look.
    CHECK(spawn_state_fresh(states.at(1), model));
    states.at(1) = FakeState{model};
    CHECK_FALSE(states.at(1).hidden);
    CHECK_FALSE(spawn_state_fresh(states.at(1), model));
    CHECK(spawn_state_fresh(states.at(1), std::make_shared<const FakeModel>()));
    // Hidden again later: dropped again once.
    CHECK(hide_spawn_state(states.at(1)));
    CHECK_FALSE(hide_spawn_state(states.at(1)));
}
