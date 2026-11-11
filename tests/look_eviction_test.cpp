// render/look_eviction.h: when an idle dressed appearance may be freed.

#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <vector>

#include "render/look_eviction.h"
#include "render/texture_claims.h"

using hailstone::render::kLookIdleMs;
using hailstone::render::kLookSweepMs;
using hailstone::render::look_idle;

TEST_CASE("Appearance texture lifetime: shared layers survive until the last model releases them") {
    struct Part {
        std::uint32_t texture = 0, texture2 = 0xffffffffu, normal_map = 0, rough_map = 0;
    };
    struct Model { std::vector<Part> parts; };
    std::map<int, Model> models;
    models[1].parts = {{1, 2, 3, 4}, {1, 0xffffffffu, 0, 0}};
    models[2].parts = {{1, 5, 3, 0}};
    auto used = hailstone::render::model_texture_slots(models);
    CHECK(used == std::unordered_set<std::uint32_t>{0, 1, 2, 3, 4, 5});
    models.erase(1); // one look leaves; its shared base and normal map stay
    used = hailstone::render::model_texture_slots(models);
    CHECK(used == std::unordered_set<std::uint32_t>{0, 1, 3, 5});
    models.clear();
    CHECK(hailstone::render::model_texture_slots(models) == std::unordered_set<std::uint32_t>{0});
}

TEST_CASE("Appearance texture lifetime: exhausted uploads retry once capacity returns; absent images stay cached") {
    using namespace hailstone::render;
    struct Part {
        std::uint32_t texture = 0, texture2 = 0xffffffffu, normal_map = 0, rough_map = 0;
    };
    struct Model {
        std::vector<Part> parts;
        bool capacity_blocked = false;
    };
    struct Slot {
        std::uint32_t slot = 0;
        bool capacity_blocked = false;
    };
    std::map<int, Model> models{{1, {{{1, 0, 2, 0}}, true}}, {2, {{{1, 0xffffffffu, 2, 0}}, false}}};
    std::map<std::string, Slot> slots{{"base", {1}}, {"normal", {2}}, {"tint", {0, true}},
                                       {"rough", {0, true}}, {"missing", {0}}};
    auto claims = std::make_shared<TextureClaims>();
    for (const auto& [key, slot] : slots) {
        (void)slot;
        claims->installed(key);
    }
    std::size_t retired = 0;
    const auto retire = [&](Model&) { ++retired; };
    const auto release = [&](const std::string& key) { claims->evicted(key); };
    CHECK_FALSE(retry_capacity_blocked_textures(models, slots, false, retire, release));
    CHECK(models.size() == 2);
    CHECK(slots.size() == 5);
    CHECK(retired == 0);
    CHECK_FALSE(claims->claim("tint"));

    REQUIRE(retry_capacity_blocked_textures(models, slots, true, retire, release));
    CHECK(retired == 1);
    CHECK_FALSE(models.contains(1)); // its baked slot 0 must be rebuilt too
    CHECK(models.contains(2));
    CHECK(slots.size() == 3);
    CHECK(slots.at("missing").slot == 0); // no image: do not repeatedly decode it
    CHECK_FALSE(claims->claim("missing"));
    CHECK(claims->claim("tint"));
    CHECK(claims->claim("rough")); // material maps retry as well as colour layers
    CHECK(model_texture_slots(models) == std::unordered_set<std::uint32_t>{0, 1, 2});
    CHECK_FALSE(retry_capacity_blocked_textures(models, slots, true, retire, release));
    CHECK(retired == 1);
}

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
