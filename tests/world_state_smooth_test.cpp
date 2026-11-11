// WorldState::smooth: the drawn place follows the spawn's and recovers from a value that is not a number.
#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "game/world_state.h"

using namespace hailstone::game;

namespace {
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

Spawn& add(WorldState& w, std::uint32_t id, glm::vec3 at) {
    Spawn& s = w.spawns[id];
    s.id = id;
    s.race = 1;
    s.is_npc = true;
    s.position = at;
    return s;
}
} // namespace

TEST_CASE("smooth: a drawn place that is not a number snaps to the spawn's next good place") {
    WorldState w;
    Spawn& s = add(w, 5, {10.0f, 20.0f, 0.0f});
    w.smooth(0.016f, 1.0);
    REQUIRE(s.display_valid);
    s.display_position = glm::vec3(kNaN);
    s.display_heading = kNaN;
    w.smooth(0.016f, 1.0);
    CHECK(s.display_position == glm::vec3(10.0f, 20.0f, 0.0f));
    CHECK(std::isfinite(s.display_heading));
}

TEST_CASE("smooth: a spawn place that is not a number keeps the last drawn place") {
    WorldState w;
    Spawn& s = add(w, 5, {10.0f, 20.0f, 0.0f});
    w.smooth(0.016f, 1.0);
    s.position = {kNaN, 20.0f, 0.0f};
    w.smooth(0.016f, 1.0);
    CHECK(s.display_position == glm::vec3(10.0f, 20.0f, 0.0f));
    s.position = {12.0f, 20.0f, 0.0f}; // a good update: smoothing goes on from there
    w.smooth(0.016f, 1.0);
    CHECK(s.display_position.x > 10.0f);
    CHECK(s.display_position.x < 12.0f);
}

TEST_CASE("smooth: the local player is drawn where it is, not eased") {
    WorldState w;
    w.player_id = 7;
    Spawn& me = add(w, 7, {10.0f, 20.0f, 0.0f});
    me.is_npc = false;
    me.heading = 100.0f;
    Spawn& other = add(w, 5, {10.0f, 20.0f, 0.0f});
    w.smooth(0.016f, 1.0);
    me.position = other.position = {12.0f, 20.0f, 0.0f};
    me.heading = other.heading = 140.0f;
    w.smooth(0.016f, 1.0);
    CHECK(me.display_position == glm::vec3(12.0f, 20.0f, 0.0f));
    CHECK(me.display_heading == 140.0f);
    CHECK(other.display_position.x < 12.0f); // others still ease
}

TEST_CASE("smooth: set_local_drawn places the player and its mount, and smooth keeps it for that frame") {
    WorldState w;
    w.player_id = 7;
    w.controlled_id = 9;
    Spawn& me = add(w, 7, {12.0f, 20.0f, 0.0f});
    Spawn& mount = add(w, 9, {12.0f, 20.0f, 0.0f});
    w.smooth(0.016f, 1.0);
    // Between two steps: the camera's place is short of the last step's.
    w.set_local_drawn({11.5f, 20.0f, 0.0f}, 30.0f);
    CHECK(me.display_position == glm::vec3(11.5f, 20.0f, 0.0f));
    CHECK(mount.display_heading == 30.0f);
    w.smooth(0.016f, 1.0);
    CHECK(me.display_position == glm::vec3(11.5f, 20.0f, 0.0f));
    CHECK(mount.display_position == glm::vec3(11.5f, 20.0f, 0.0f));
    // A frame without the call (update_player did not run): drawn at the position.
    w.smooth(0.016f, 1.0);
    CHECK(me.display_position == glm::vec3(12.0f, 20.0f, 0.0f));
    // A place that is not a number is not taken.
    w.set_local_drawn({kNaN, 20.0f, 0.0f}, 30.0f);
    CHECK(me.display_position == glm::vec3(12.0f, 20.0f, 0.0f));
}

TEST_CASE("smooth: remote climbing contact follows movement steps and clears on the next free move") {
    WorldState w;
    Spawn& moving = add(w, 5, {1.0f, 2.0f, 3.0f});
    moving.speed_run = 1.0f;
    w.physics_steps = std::vector<float>{16.0f, 17.0f};
    int probes = 0;
    int result = 6;
    WorldState::RemoteCollisionFn collision = [&](const Spawn& actor, const glm::vec3& from, const glm::vec3& to) {
        CHECK(actor.id == 5);
        CHECK(to.x > from.x);
        ++probes;
        return result;
    };
    w.smooth(0.033f, 1.0, {}, {}, {}, {}, collision);
    CHECK(probes == 2);
    CHECK(moving.colliding_type == 6);

    result = 0;
    w.smooth(0.033f, 1.033, {}, {}, {}, {}, collision);
    CHECK(probes == 4);
    CHECK(moving.colliding_type == 0);

    moving.colliding_type = 6;
    moving.speed_run = 0.0f;
    moving.velocity = glm::vec3(0.0f);
    w.smooth(0.033f, 1.066, {}, {}, {}, {}, collision);
    CHECK(probes == 4);
    CHECK(moving.colliding_type == 6); // The standing-still shortcut retains contact.

    moving.speed_run = 1.0f;
    moving.properties.push_back(0x64); // NoCollide: no world probe.
    w.smooth(0.033f, 1.099, {}, {}, {}, {}, collision);
    CHECK(probes == 4);
    CHECK(moving.colliding_type == 0);
}

TEST_CASE("smooth: controlled players, mounted riders and stale spawns do not acquire remote contacts") {
    WorldState w;
    w.player_id = 1;
    w.controlled_id = 2;
    for (std::uint32_t id : {1u, 2u, 3u, 4u}) {
        auto& spawn = add(w, id, {1, 2, 3});
        spawn.speed_run = 1.0f;
        spawn.update_time = 1.0;
    }
    w.spawns.at(3).mount_id = 2;
    w.spawns.at(4).update_time = -20.0;
    int probes = 0;
    w.smooth(0.016f, 1.0, {}, {}, {}, {},
             [&](const Spawn&, const glm::vec3&, const glm::vec3&) { ++probes; return 6; });
    CHECK(probes == 0);
    for (std::uint32_t id : {1u, 2u, 3u, 4u}) CHECK(w.spawns.at(id).colliding_type == 0);
}
