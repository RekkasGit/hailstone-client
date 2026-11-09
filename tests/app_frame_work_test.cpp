// The per-frame work the client's glue keeps from frame to frame instead of rebuilding (app/group_ui.h and others).

#include <doctest/doctest.h>

#include <string>

#include "app/group_ui.h"
#include "game/world_state.h"

using namespace hailstone;

namespace {

game::Spawn spawn(std::uint32_t id, std::string name, bool corpse = false) {
    game::Spawn s;
    s.id = id;
    s.name = std::move(name);
    s.is_corpse = corpse;
    return s;
}

} // namespace

TEST_CASE("Group window: a member's spawn by its name, EQEmu's digits and case aside") {
    CHECK(app::group_member_is("Soandso", "Soandso"));
    CHECK(app::group_member_is("Soandso", "soandso000"));
    CHECK(app::group_member_is("soandso", "SOANDSO12"));
    CHECK_FALSE(app::group_member_is("Soandso", "Soandsoa"));
    CHECK_FALSE(app::group_member_is("Soand", "Soandso"));
    CHECK_FALSE(app::group_member_is("Soandso", "123"));
}

TEST_CASE("Group window: each member's spawn is kept between frames and looked for again only when it may have come") {
    game::WorldState world;
    world.spawns[5] = spawn(5, "a_rat01");
    world.spawns[7] = spawn(7, "Soandso000");
    world.spawns[9] = spawn(9, "Soandso's_corpse", true);
    app::GroupMemberSpawns cache;
    std::int64_t now = 1000;
    auto frame = [&](const std::string& name) {
        cache.begin();
        const game::Spawn* s = cache.find(world, name, now);
        cache.end();
        return s ? s->id : 0u;
    };
    CHECK(frame("Soandso") == 7);
    CHECK(cache.scans() == 1);
    // Found: checked by its id, with no search.
    for (int i = 0; i < 10; ++i) CHECK(frame("Soandso") == 7);
    CHECK(cache.scans() == 1);
    // A member in another zone: searched for once, then again when the spawns change or after a second.
    CHECK(frame("Farfaraway") == 0);
    CHECK(cache.scans() == 2);
    now += 500;
    CHECK(frame("Farfaraway") == 0);
    CHECK(cache.scans() == 2);
    world.spawns[11] = spawn(11, "Farfaraway");
    CHECK(frame("Farfaraway") == 11);
    CHECK(cache.scans() == 3);
    // Gone (zoned, camped): searched for again at once; a corpse is not the member.
    world.spawns.erase(7);
    CHECK(frame("Soandso") == 0);
    // Another spawn taking the id is not the member either.
    world.spawns[11] = spawn(11, "Someoneelse");
    CHECK(frame("Farfaraway") == 0);
    // Back after a second although the count is the same.
    world.spawns[11] = spawn(11, "Farfaraway01");
    CHECK(frame("Farfaraway") == 0);
    now += 1000;
    CHECK(frame("Farfaraway") == 11);
}
