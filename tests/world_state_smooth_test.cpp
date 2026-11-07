// WorldState::smooth: the drawn place follows the spawn's and recovers from a value that is not a number.
#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "game/world_state.h"

using namespace nmsc::game;

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
