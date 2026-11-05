#include <doctest/doctest.h>

#include <cstring>
#include <limits>
#include <vector>

#include "game/missiles.h"

using namespace nmsc;
using namespace nmsc::game;

namespace {

// An OP_SomeItemPacketMaybe as EQEmu's RoF2 translator writes it (rof2.cpp ENCODE(OP_SomeItemPacketMaybe),
// rof2_structs.h Arrow_Struct offsets), independent of the decoder.
std::vector<std::byte> eqemu_arrow(float src_y, float src_x, float src_z, float velocity, float launch_angle,
                                   float tilt, float arc, std::uint32_t source, std::uint32_t target,
                                   std::uint8_t skill, std::uint8_t item_type, const char* model) {
    std::vector<std::byte> b(116);
    auto putf = [&](std::size_t at, float f) { std::memcpy(&b[at], &f, 4); };
    auto put32 = [&](std::size_t at, std::uint32_t v) { std::memcpy(&b[at], &v, 4); };
    putf(0, src_y);
    putf(4, src_x);
    putf(8, src_z);
    putf(24, velocity);
    putf(28, launch_angle);
    putf(32, tilt);
    putf(44, arc);
    put32(48, source);
    put32(52, target);
    put32(56, 8005);
    b[70] = std::byte{175};
    b[73] = std::byte{skill};
    b[74] = std::byte{item_type};
    std::memcpy(&b[89], model, std::strlen(model));
    return b;
}

struct TestWorld {
    std::vector<MissileSpawn> spawns;
    bool solid = false;
    MissileWorld world() {
        MissileWorld w;
        w.spawn = [this](std::uint32_t id) -> std::optional<MissileSpawn> {
            for (const auto& s : spawns)
                if (s.id == id) return s;
            return std::nullopt;
        };
        w.sweep = [this](const glm::vec3&, const glm::vec3&, float) { return solid; };
        w.inside = [](const glm::vec3&) { return true; };
        return w;
    }
};

} // namespace

TEST_CASE("missile packet: EQEmu's skill lands in the range byte") {
    auto b = eqemu_arrow(10.0f, 20.0f, 3.0f, 4.0f, 128.0f, 125.0f, 50.0f, 7, 9, 7, 27, "IT10");
    auto p = decode_missile_packet(b);
    REQUIRE(p);
    CHECK(p->position == glm::vec3(10.0f, 20.0f, 3.0f));
    CHECK(p->speed == 4.0f);
    CHECK(p->heading == 128.0f);
    CHECK(p->tilt == 125.0f);
    CHECK(p->pitch == 50.0f);
    CHECK(p->source_id == 7);
    CHECK(p->target_id == 9);
    CHECK(p->item_id == 8005);
    CHECK(p->range == 7);
    CHECK(p->item_type == 27);
    CHECK(p->skill == 0);
    CHECK_FALSE(p->to_location);
    CHECK(p->model == "IT10");
    CHECK_FALSE(decode_missile_packet(std::span(b).first(115)));
}

TEST_CASE("missile packet: a NaN or infinite position, speed or angle is refused") {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    CHECK_FALSE(decode_missile_packet(eqemu_arrow(nan, 0.0f, 0.0f, 4.0f, 0.0f, 125.0f, 50.0f, 7, 9, 7, 27, "IT10")));
    CHECK_FALSE(decode_missile_packet(eqemu_arrow(0.0f, 0.0f, 0.0f, inf, 0.0f, 125.0f, 50.0f, 7, 9, 7, 27, "IT10")));
    CHECK_FALSE(decode_missile_packet(eqemu_arrow(0.0f, 0.0f, 0.0f, 4.0f, -inf, 125.0f, 50.0f, 7, 9, 7, 27, "IT10")));
    CHECK_FALSE(decode_missile_packet(eqemu_arrow(0.0f, 0.0f, 0.0f, 4.0f, 0.0f, nan, 50.0f, 7, 9, 7, 27, "IT10")));
    CHECK_FALSE(decode_missile_packet(eqemu_arrow(0.0f, 0.0f, 0.0f, 4.0f, 0.0f, 125.0f, nan, 7, 9, 7, 27, "IT10")));
    auto b = eqemu_arrow(0.0f, 0.0f, 0.0f, 4.0f, 0.0f, 125.0f, 50.0f, 7, 9, 7, 27, "IT10");
    float v = nan;
    std::memcpy(&b[12], &v, 4); // velocity x
    CHECK_FALSE(decode_missile_packet(b));
}

TEST_CASE("missile launch animation and delay") {
    CHECK(missile_launch_animation(0, 27, 7, 1) == 9);  // a bow shot
    CHECK(missile_launch_animation(0, 28, 7, 1) == 10);
    CHECK(missile_launch_animation(0, 20, 14, 1) == 5); // an NMS bolt (item type 20, Conjuration)
    CHECK(missile_launch_animation(0, 9, 0, 1) == 44);
    CHECK(missile_launch_animation(10, 0, 0, 0x80) == 6);
    // New-style sources (FUN_0059f8d0): BATK for arrows, BLPR for others, WATK in water.
    CHECK(missile_launch_animation(0, 27, 7, 522, 0, 0, true) == 0x5d);
    CHECK(missile_launch_animation(0, 20, 14, 522, 0, 0, true) == 0x51);
    CHECK(missile_launch_animation(0, 20, 14, 522, 0, 5, true) == 0x5e);
    CHECK(missile_launch_animation(0, 9, 0, 522, 0, 0, true) == 0x87);
    CHECK(missile_launch_delay(1000, 7) == 700);
    CHECK(missile_launch_delay(0, 7) == 1500);
    CHECK(missile_launch_delay(1000, 0) == 0);
}

TEST_CASE("missile actor scale") {
    CHECK(missile_actor_scale("IT10", 1) == 1.0f);
    CHECK(missile_actor_scale("it66", 1) == 1.5f);
    CHECK(missile_actor_scale("IT74", 1) == 1.3f);
    CHECK(missile_actor_scale("IT74", 0x3c) == 0.8f);
}

TEST_CASE("cmath angle") {
    CHECK(cmath_angle512(10.0f, 0.0f) == 128.0f);
    CHECK(cmath_angle512(10.0f, 20.0f) == 37.0f); // atan(0.5) = 37.8 512ths
    CHECK(cmath_angle512(0.0f, 10.0f) == 0.0f);
}

TEST_CASE("an arrow waits for the launch, flies, homes and hits") {
    TestWorld tw;
    tw.spawns.push_back({7, {0.0f, 0.0f, 5.0f}, 3.75f, 6.0f, 1, false});
    tw.spawns.push_back({9, {0.0f, 150.0f, 5.0f}, 3.75f, 6.0f, 1, false});
    MissileWorld w = tw.world();
    // Launched along +x while the target stands along +y: the homing turns it.
    auto p = decode_missile_packet(eqemu_arrow(0.0f, 0.0f, 5.0f, 4.0f, 0.0f, 125.0f, 50.0f, 7, 9, 7, 27, "IT10"));
    REQUIRE(p);
    MissileList list;
    list.add(make_missile(*p, 1000, 700));
    list.update(1500, w);
    REQUIRE(list.missiles().size() == 1);
    CHECK_FALSE(list.missiles()[0].has_actor); // waiting for the bow
    list.update(1700, w);
    REQUIRE(list.missiles().size() == 1);
    CHECK(list.missiles()[0].has_actor);
    CHECK(list.missiles()[0].radius == 0.1f);
    CHECK(list.missiles()[0].ends_at == 7700);
    std::uint32_t now = 1700;
    float closest = 1e9f;
    while (!list.missiles().empty() && now < 9000) {
        now += 33;
        list.update(now, w);
        if (!list.missiles().empty()) {
            const auto& m = list.missiles()[0];
            closest = std::min(closest, glm::length(glm::vec2(m.position) - glm::vec2(0.0f, 150.0f)));
        }
    }
    CHECK(list.missiles().empty());
    CHECK(now < 7700); // it hit before its 6 s ran out
    CHECK(closest < 30.0f);
}

TEST_CASE("missiles end at their lifetime or on a wall") {
    TestWorld tw;
    tw.spawns.push_back({7, {0.0f, 0.0f, 5.0f}, 3.75f, 6.0f, 1, false});
    MissileWorld w = tw.world();
    auto p = decode_missile_packet(eqemu_arrow(0.0f, 0.0f, 5.0f, 1.0f, 0.0f, 125.0f, 0.0f, 7, 0, 7, 27, "IT10"));
    REQUIRE(p);
    MissileList list;
    list.add(make_missile(*p, 0, 0));
    std::uint32_t now = 0;
    while (!list.missiles().empty() && now < 20000) {
        now += 33;
        list.update(now, w);
    }
    CHECK(now > 6000);
    CHECK(now < 6100);

    tw.solid = true;
    w = tw.world();
    list.add(make_missile(*p, 0, 0));
    list.update(10, w);  // the actor
    list.update(50, w);  // the first step hits the wall
    CHECK(list.missiles().empty());

    // No source spawn: dropped.
    auto q = *p;
    q.source_id = 99;
    list.add(make_missile(q, 0, 0));
    list.update(10, w);
    CHECK(list.missiles().empty());
}

TEST_CASE("a missile with a target flies through walls until the target goes; its source going ends it") {
    TestWorld tw;
    tw.spawns.push_back({7, {0.0f, 0.0f, 5.0f}, 3.75f, 6.0f, 1, false});
    tw.spawns.push_back({9, {0.0f, 400.0f, 5.0f}, 3.75f, 6.0f, 1, false});
    tw.solid = true; // every sweep hits
    MissileWorld w = tw.world();
    auto p = decode_missile_packet(eqemu_arrow(0.0f, 0.0f, 5.0f, 4.0f, 0.0f, 125.0f, 50.0f, 7, 9, 7, 27, "IT10"));
    REQUIRE(p);
    MissileList list;
    list.add(make_missile(*p, 0, 0));
    list.update(10, w); // the actor
    for (std::uint32_t now = 50; now < 500; now += 40) list.update(now, w);
    REQUIRE(list.missiles().size() == 1); // MoveMissile sweeps only without a target
    CHECK(list.missiles()[0].target_id == 9);

    // DeleteMyMissiles: the target despawns, the missile loses it and the next sweep ends it.
    tw.spawns.pop_back();
    list.update(540, w);
    CHECK(list.missiles().empty());

    // A target not known when the missile comes: no target from the start.
    list.add(make_missile(*p, 0, 0));
    list.update(600, w);
    REQUIRE(list.missiles().size() == 1);
    CHECK(list.missiles()[0].target_id == 0);
    list.clear();

    // The source despawns in flight: the missile is deleted.
    tw.solid = false;
    tw.spawns.push_back({9, {0.0f, 400.0f, 5.0f}, 3.75f, 6.0f, 1, false});
    list.add(make_missile(*p, 0, 0));
    list.update(10, w);
    list.update(50, w);
    REQUIRE(list.missiles().size() == 1);
    tw.spawns.erase(tw.spawns.begin());
    list.update(90, w);
    CHECK(list.missiles().empty());
}
