#include <doctest/doctest.h>

#include "game/mechanics/touching_switch.h"

using namespace hailstone::game::mechanics;

namespace {

TouchHooks no_zone_files() {
    TouchHooks h;
    h.random_percent = [] { return 0; };
    h.zone_point_zone = [](std::int32_t) { return -1; };
    h.zone_s3d_exists = [](std::int32_t) { return false; };
    h.zone_eqg_exists = [](std::int32_t) { return false; };
    return h;
}

} // namespace

TEST_CASE("TouchingSwitch: a touch trap hurts once a second per switch") {
    TouchedSwitch sw;
    sw.id = 3;
    sw.type = 0x78; // SPEARDOWN: acts on touch
    sw.state = 2;
    sw.repop_ms = 5000;
    sw.damage = 30;
    TouchSpawn me;
    me.spawn_id = 7;
    std::optional<std::uint8_t> last;
    auto r = touching_switch(sw, me, 10000, last, no_zone_files());
    REQUIRE(r.damage);
    CHECK(*r.damage == 30);
    CHECK(r.clear);
    CHECK(last == std::optional<std::uint8_t>(3));
    CHECK(me.last_touch_ms == 10000);
    // Again within the second: nothing, and the switch is kept.
    r = touching_switch(sw, me, 10500, last, no_zone_files());
    CHECK_FALSE(r.damage);
    CHECK_FALSE(r.clear);
    // Another switch is not held back by the first one's time.
    TouchedSwitch other = sw;
    other.id = 4;
    r = touching_switch(other, me, 10600, last, no_zone_files());
    CHECK(r.damage);
    // A quick repop (under 1000 ms) allows a hit every 400 ms.
    sw.repop_ms = 500;
    last = sw.id;
    me.last_touch_ms = 20000;
    CHECK_FALSE(touching_switch(sw, me, 20399, last, no_zone_files()).damage);
    CHECK(touching_switch(sw, me, 20400, last, no_zone_files()).damage);
}

TEST_CASE("TouchingSwitch: high types that do not act on touch keep the switch") {
    TouchedSwitch sw;
    sw.type = 0x99;
    TouchSpawn me;
    std::optional<std::uint8_t> last;
    auto r = touching_switch(sw, me, 1000, last, no_zone_files());
    CHECK_FALSE(r.clear);
    CHECK_FALSE(r.damage);
    CHECK_FALSE(r.use);
}

TEST_CASE("TouchingSwitch: a sprung trap is forgotten") {
    TouchedSwitch sw;
    sw.type = 0x78;
    sw.state = 4;
    TouchSpawn me;
    std::optional<std::uint8_t> last;
    auto r = touching_switch(sw, me, 1000, last, no_zone_files());
    CHECK(r.clear);
    CHECK_FALSE(r.damage);
}

TEST_CASE("TouchingSwitch: type 0x23 is used by nobody on a roll above 50, once a second") {
    TouchedSwitch sw;
    sw.type = 0x23;
    TouchSpawn me;
    me.last_touch_ms = 0;
    std::optional<std::uint8_t> last;
    TouchHooks h = no_zone_files();
    h.random_percent = [] { return 51; };
    auto r = touching_switch(sw, me, 1001, last, h);
    REQUIRE(r.use);
    CHECK(r.use->user == 0);
    CHECK(r.use->key == 0);
    CHECK_FALSE(touching_switch(sw, me, 1500, last, h).use);
    h.random_percent = [] { return 50; };
    CHECK_FALSE(touching_switch(sw, me, 3000, last, h).use);
    CHECK(me.last_touch_ms == 3000);
}

TEST_CASE("TouchingSwitch: type 0x39 needs the destination zone's files or a door id") {
    TouchedSwitch sw;
    sw.type = 0x39;
    sw.param = 20;
    TouchSpawn me;
    me.spawn_id = 9;
    std::optional<std::uint8_t> last;
    TouchHooks h = no_zone_files();
    // EQEmu's records: param -1, no adventure or dynamic zone id, no script: never used on touch.
    CHECK_FALSE(touching_switch(sw, me, 5000, last, h).use);
    // Every 2 s at most.
    h.zone_point_zone = [](std::int32_t p) { return p == 20 ? 22 : -1; };
    h.zone_s3d_exists = [](std::int32_t z) { return z == 22; };
    CHECK_FALSE(touching_switch(sw, me, 6000, last, h).use);
    auto r = touching_switch(sw, me, 7001, last, h);
    REQUIRE(r.use);
    CHECK(r.use->user == 9);
    CHECK(r.use->key == -1);
    // The cursor item is the key.
    me.cursor_item_id = 1234;
    r = touching_switch(sw, me, 9002, last, h);
    REQUIRE(r.use);
    CHECK(r.use->key == 1234);
    // A script flag alone is enough; an NPC never uses it.
    h = no_zone_files();
    sw.has_script = 1;
    CHECK(touching_switch(sw, me, 12000, last, h).use);
    me.spawn_type = 1;
    CHECK_FALSE(touching_switch(sw, me, 15000, last, h).use);
}

TEST_CASE("TouchingSwitch: type 0x32 opens closed, closes open after its repop time") {
    TouchedSwitch sw;
    sw.type = 0x32;
    sw.state = 0;
    sw.repop_ms = 10000;
    TouchSpawn me;
    me.spawn_id = 5;
    std::optional<std::uint8_t> last;
    auto r = touching_switch(sw, me, 100, last, no_zone_files());
    REQUIRE(r.use);
    CHECK(r.use->user == 5);
    CHECK(r.use->key == -1);
    CHECK(me.last_touch_ms == 100);
    sw.state = 1;
    CHECK_FALSE(touching_switch(sw, me, 10100, last, no_zone_files()).close_now);
    CHECK(touching_switch(sw, me, 10101, last, no_zone_files()).close_now);
    // NPCs do nothing with it.
    me.spawn_type = 1;
    sw.state = 0;
    CHECK_FALSE(touching_switch(sw, me, 20000, last, no_zone_files()).use);
}
