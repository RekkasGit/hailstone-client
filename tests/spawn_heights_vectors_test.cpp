// The spawn height ports (game/spawn_heights.h) against the original's
// vectors (rof2decomp batch 259, 7eb07518): SetHeights (3000),
// ComputeSpecialHeights (1800), ChangeHeight (1500), FUN_005062a0 the
// collision-volume radius (2000), CalcScaleFactorForHeight (1500) and
// GetEyeHeight (2000). Every float is compared bit for bit (the getters'
// unrounded results as doubles). Then the race table's new-style flag, the
// game files' tables and the zone-in placement.
#include <doctest/doctest.h>

#include <bit>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "game/mechanics/collision_movement.h"
#include "game/race_models.h"
#include "game/spawn_heights.h"
#include "game/world_state.h"
#include "game_files.h"
#include "vector_files.h"

using hailstone::test::Json;
namespace g = hailstone::game;
namespace mech = hailstone::game::mechanics;

namespace {

mech::Fpu fpu_of(const Json& r) {
    return mech::Fpu(r["precision"].text == "x87-24" ? mech::FpuPrecision::Single24 : mech::FpuPrecision::Double53);
}

std::string hex(std::uint32_t v) { return std::format("{:#010x}", v); }

float f32(const Json& j) { return hailstone::test::f32_of(j); }

// "" or "<name> <have> want <want>".
std::string same(const char* name, float have, const Json& want) {
    std::uint32_t h = std::bit_cast<std::uint32_t>(have), w = hailstone::test::f32_bits(want);
    return h == w ? "" : std::format("{} {} want {}; ", name, hex(h), hex(w));
}

std::string same64(double have, const Json& want) {
    std::uint64_t h = std::bit_cast<std::uint64_t>(have), w = hailstone::test::f64_bits(want);
    return h == w ? "" : std::format("value {:#018x} want {:#018x}", h, w);
}

// The inputs SetHeights / ChangeHeight records give one spawn.
g::HeightInputs inputs_of(const Json& spawn, const Json& in) {
    g::HeightInputs h;
    h.race = spawn["race"].i32();
    h.new_style_model = spawn["new_style_model"].truthy();
    h.has_race_manager = in.has("has_race_manager") ? in["has_race_manager"].truthy() : true;
    h.racedata_height = in.has("racedata_height") ? f32(in["racedata_height"]) : 6.0f; // ChangeHeight's fake: 6.0
    h.roffset = f32(in["roffset"]);
    h.old_model_scale = in["old_model_scale"].truthy();
    return h;
}

std::string heights_differ(const g::SpawnHeights& s, const Json& out) {
    std::string why;
    why += same("height", s.height, out["height"]);
    why += same("avatar_height", s.avatar_height, out["avatar_height"]);
    why += same("model_scale", s.model_scale, out["model_scale"]);
    why += same("view_height", s.view_height, out["view_height"]);
    why += same("camera_offset", s.camera_offset, out["camera_offset"]);
    why += same("animation_scale", s.animation_scale, out["animation_scale"]);
    why += same("length", s.length, out["length"]);
    why += same("jump_strength", s.jump_strength, out["jump_strength"]);
    why += same("swim_strength", s.swim_strength, out["swim_strength"]);
    why += same("max_speak_distance", s.max_speak_distance, out["max_speak_distance"]);
    return why;
}

} // namespace

TEST_CASE("vectors: SetHeights") {
    hailstone::test::each_vector("SetHeights.jsonl", [](const Json& r) {
        const Json& in = r["in"];
        g::SpawnHeights s;
        s.height = f32(in["height"]);
        // The fields SetHeights overwrites had junk before; it reads none of them.
        bool asked = g::set_heights(s, inputs_of(in, in), f32(in["view_offset"]), f32(in["anim_scale"]), fpu_of(r));
        std::string why = heights_differ(s, r["out"]);
        if (asked != r["out"]["racedata_asked"].truthy()) why += "racedata_asked differs";
        return why;
    });
}

TEST_CASE("vectors: ComputeSpecialHeights") {
    hailstone::test::each_vector("ComputeSpecialHeights.jsonl", [](const Json& r) {
        const Json& in = r["in"];
        float scale = f32(in["scale"]), eye = f32(in["eye"]), avatar = f32(in["avatar_height"]),
              length = f32(in["length"]);
        g::compute_special_heights(in["race"].i32(), scale, eye, avatar, length, fpu_of(r));
        const Json& out = r["out"];
        return same("scale", scale, out["scale"]) + same("eye", eye, out["eye"]) +
               same("avatar_height", avatar, out["avatar_height"]) + same("length", length, out["length"]);
    });
}

TEST_CASE("vectors: ChangeHeight") {
    hailstone::test::each_vector("ChangeHeight.jsonl", [](const Json& r) {
        const Json& in = r["in"];
        const bool mounted = in["mounted"].truthy();
        g::SpawnHeights heights[2];
        g::HeightLink chain[2];
        const char* names[2] = {"spawn", "mount"};
        for (int k = 0; k < (mounted ? 2 : 1); ++k) {
            const Json& sp = in[names[k]];
            heights[k].height = f32(sp["height"]);
            chain[k].heights = &heights[k];
            chain[k].inputs = inputs_of(sp, in);
            chain[k].type = static_cast<std::uint8_t>(sp["type"].i32());
            chain[k].has_actor = sp["has_actor"].truthy();
            chain[k].mount_race = in["is_mount_race"].truthy();
        }
        auto updates = g::change_height(std::span(chain, mounted ? 2 : 1), f32(in["size"]), f32(in["view_offset"]),
                                        f32(in["anim_scale"]), fpu_of(r));
        const Json& out = r["out"];
        std::string why;
        for (int k = 0; k < (mounted ? 2 : 1); ++k) why += heights_differ(heights[k], out[names[k]]);
        const auto& scales = out["actor_scale_calls"].items;
        if (scales.size() != updates.size()) return why + "actor scale call count differs";
        std::size_t radius_at = 0;
        const auto& radii = out["volume_radius_calls"].items;
        for (std::size_t n = 0; n < updates.size(); ++n) {
            if (static_cast<std::size_t>(scales[n]["spawn"].i32()) != updates[n].spawn || scales[n]["flag"].i32() != 1)
                why += "actor scale call differs; ";
            why += same("actor scale", updates[n].scale, scales[n]["scale"]);
            if (updates[n].volume_radius) {
                if (radius_at >= radii.size()) return why + "an extra volume radius call";
                if (static_cast<std::size_t>(radii[radius_at]["spawn"].i32()) != updates[n].spawn)
                    why += "volume radius spawn differs; ";
                why += same("volume radius", *updates[n].volume_radius, radii[radius_at]["radius"]);
                ++radius_at;
            }
        }
        if (radius_at != radii.size()) why += "volume radius call count differs";
        return why;
    });
}

TEST_CASE("vectors: GetCollisionVolumeRadius (FUN_005062a0)") {
    hailstone::test::each_vector("GetCollisionVolumeRadius.jsonl", [](const Json& r) {
        const Json& in = r["in"];
        double v = mech::collision_volume_radius(f32(in["avatar_height"]), in["type"].i32() == 1,
                                                 in["race"].u32(), in["is_mount_race"].truthy(), fpu_of(r));
        return same64(v, r["out"]["value"]);
    });
}

TEST_CASE("vectors: CalcScaleFactorForHeight") {
    hailstone::test::each_vector("CalcScaleFactorForHeight.jsonl", [](const Json& r) {
        const Json& in = r["in"];
        double v = g::calc_scale_factor_for_height(f32(in["height"]), in["new_style_model"].truthy(),
                                                   in["old_model_scale"].truthy(), fpu_of(r));
        return same64(v, r["out"]["value"]);
    });
}

TEST_CASE("vectors: GetEyeHeight") {
    hailstone::test::each_vector("GetEyeHeight.jsonl", [](const Json& r) {
        const Json& in = r["in"];
        g::EyeInputs e;
        e.uses_new_style_model = in["uses_new_style_model"].truthy();
        e.definition_eye = f32(in["definition_eye"]);
        e.model_scale = f32(in["model_scale"]);
        e.camera_offset = f32(in["camera_offset"]);
        e.mounted = in["mounted"].truthy();
        e.race = in["race"].i32();
        e.view_height = f32(in["view_height"]);
        e.avatar_height = f32(in["avatar_height"]);
        return same64(g::eye_height(e, fpu_of(r)), r["out"]["value"]);
    });
}

TEST_CASE("spawn heights: the new-style races are the race table's flag 0x08 records") {
    auto listed = [](std::int32_t race) {
        static const std::int32_t kRanges[][2] = {{197, 197}, {374, 374}, {403, 403}, {405, 414}, {417, 442},
                                                  {445, 501}, {503, 599}, {601, 645}, {647, 732}};
        for (const auto& range : kRanges)
            if (race >= range[0] && race <= range[1]) return true;
        return false;
    };
    for (std::int32_t race = -1; race <= 3000; ++race) CHECK_MESSAGE(g::is_new_style_model_race(race) == listed(race), race);
}

TEST_CASE("spawn heights: the human baseline and character select") {
    g::SpawnHeights s;
    s.height = 6.0f;
    g::HeightInputs in = g::height_inputs(nullptr, 1, 0, false);
    g::set_heights(s, in, 0.0f, 1.0f);
    CHECK(s.model_scale == 1.2f);
    CHECK(s.avatar_height == 3.75000024f); // 1.2 (unrounded) x 3.125, stored
    CHECK(s.view_height == 7.44f);
    CHECK(g::eye_height(s, 1) == doctest::Approx(3.69f));
    in.old_model_scale = true; // 0xddfba4 = 1 until the first OP_NewZone
    g::set_heights(s, in, 0.0f, 1.0f);
    CHECK(s.model_scale == doctest::Approx(0.923076928f));
    CHECK(s.avatar_height == doctest::Approx(2.88461542f));
    // A new-style model in zone: Height / 6 x 1.3.
    g::SpawnHeights d;
    d.height = 6.0f;
    g::set_heights(d, g::height_inputs(nullptr, 522, 0, false), 0.0f, 1.0f);
    CHECK(d.model_scale == 1.3f);
    CHECK(d.avatar_height == 4.0625f);
}

TEST_CASE("spawn heights: CreatePlayerActor's fix-ups and the in-world raise") {
    CHECK(g::actor_height_fixup(9, 8.0f) == 7.5f);
    CHECK(g::actor_height_fixup(7, 5.5f) == 5.3f);
    CHECK(!g::actor_height_fixup(9, 8.5f));
    CHECK(!g::actor_height_fixup(1, 6.0f));
    std::array<float, 3> safe{1.0f, 2.0f, 3.0f};
    int tries = 0;
    auto p = g::place_in_world({10.0f, 20.0f, -5.0f}, safe, [&](const std::array<float, 3>& q) {
        ++tries;
        return q[2] >= 0.0f;
    });
    CHECK(p == std::array<float, 3>{10.0f, 20.0f, 1.0f});
    CHECK(tries == 4); // -5, -3, -1, 1
    tries = 0;
    p = g::place_in_world({10.0f, 20.0f, -50.0f}, safe, [&](const std::array<float, 3>&) { ++tries; return false; });
    CHECK(p == safe);
    CHECK(tries == 5); // the position and four raises
}

TEST_CASE("spawn heights: the live racedata.txt layout") {
    auto line = [](int race, const char* gender, const char* male, const char* female, const char* code) {
        std::string l = std::to_string(race) + "^" + gender;
        for (int i = 0; i < 44; ++i) l += "^0";
        return l + "^" + male + "^" + female + "^^0^" + code + "^0^1^0^0^1^60^\n";
    };
    auto tables = g::parse_height_tables(
        line(188, "2", "20", "20", "fsg") + line(189, "0", "9", "9", "STM") + line(189, "1", "8", "0.5", "STF"), "");
    CHECK(tables.default_height(188, 0) == 20.0f);
    CHECK(tables.default_height(188, 1) == 20.0f);
    CHECK(tables.default_height(189, 0) == 9.0f);
    CHECK(tables.default_height(189, 1) == 6.0f); // below 1.0
    CHECK(tables.racedata_code(188, 0) == "FSG"); // gender 2's row
    CHECK(tables.racedata_code(189, 1) == "STF");
    CHECK(tables.racedata_code(189, 0) == "STM");
    CHECK(tables.racedata_code(1, 0).empty());
}

TEST_CASE("spawn heights: racedata.txt and moddat.ini") {
    // A synthetic row: race ^ 88 zero fields ^ male ^ female ^ rider.
    std::string row = "9";
    for (int i = 0; i < 88; ++i) row += "^0";
    row += "^8^0.5^\n";
    auto tables = g::parse_height_tables(row, "[TRM]\nROffset=4.5\nFPCOffset=-.6\n[hum]\nrOffset = 3.0\n");
    CHECK(tables.default_height(9, 0) == 8.0f);
    CHECK(tables.default_height(9, 1) == 6.0f); // below 1.0
    CHECK(tables.default_height(9, 2) == 8.0f);
    CHECK(tables.default_height(10, 0) == 6.0f); // no row
    CHECK(tables.roffset("TRM") == 4.5f);
    CHECK(tables.roffset("HUM") == 3.0f);
    CHECK(tables.roffset("ELF") == 3.125f);
    CHECK(tables.fpc_offset("trm") == -0.6f);
    CHECK(tables.fpc_offset("HUM") == 0.0f); // none: 0
    if (auto game = hailstone::test::game_dir()) {
        auto racedata = game->read("racedata.txt");
        auto moddat = game->read("Resources/moddat.ini");
        REQUIRE(racedata);
        REQUIRE(moddat);
        auto text = [](const std::vector<std::byte>& b) { return std::string(reinterpret_cast<const char*>(b.data()), b.size()); };
        auto real = g::parse_height_tables(text(*racedata), text(*moddat));
        CHECK(real.default_height(1, 0) == 6.0f);  // human
        CHECK(real.default_height(9, 0) == 8.0f);  // troll
        CHECK(real.default_height(522, 1) == 5.7f); // Drakkin female
        CHECK(real.roffset("HUM") == 3.125f);
        CHECK(real.roffset("SDR") == 0.776f);
        CHECK(real.fpc_offset("HUM") == 0.5f);
    }
}

TEST_CASE("spawn heights: the world's spawns through OP_NewZone, the actor fix-up and OP_ChangeSize") {
    g::WorldState w; // old_model_scale: 1 until the first OP_NewZone
    g::Spawn troll;
    troll.race = 9;
    troll.heights.height = 8.0f;
    w.set_heights(troll);
    CHECK(troll.heights.model_scale == doctest::Approx(1.6f / 1.3f));
    w.spawns[7] = troll;
    // No actor yet: nothing changes.
    w.make_actors([](const g::Spawn&) { return std::optional<g::WorldState::ActorModel>(); });
    CHECK(w.spawns[7].heights.height == 8.0f);
    w.set_old_model_scale(false); // OP_NewZone with +0x2b4 = 0: SetHeights(0, 1.0) for every spawn
    g::Spawn& t = w.spawns[7];
    CHECK(t.heights.model_scale == doctest::Approx(1.6f));
    CHECK(t.heights.avatar_height == doctest::Approx(5.0f));
    const float eye = t.heights.view_height;
    // The actor (a model with a tunic point): Troll 8 -> 7.5, the ViewHeight of size 8 kept.
    w.make_actors([](const g::Spawn&) { return std::optional(g::WorldState::ActorModel{true, false}); });
    CHECK(t.actor_made);
    CHECK(t.heights.height == 7.5f);
    CHECK(t.heights.model_scale == doctest::Approx(1.5f));
    CHECK(t.heights.avatar_height == doctest::Approx(4.6875f));
    CHECK(t.heights.view_height == eye);
    // OP_ChangeSize.
    w.change_height(t, 12.0f, 0.0f, 1.0f);
    CHECK(t.heights.height == 12.0f);
    CHECK(t.heights.model_scale == doctest::Approx(2.4f));
    CHECK(t.heights.avatar_height == doctest::Approx(7.5f));
    CHECK(t.heights.view_height == doctest::Approx(14.88f));
}

TEST_CASE("spawn heights: spawns at rest stand on the floor (the landing check)") {
    g::WorldState w;
    g::Spawn dwarf;
    dwarf.race = 8;
    dwarf.heights.avatar_height = 3.0f;
    dwarf.update_time = 1.0;
    // EQEmu's GetZOffset puts a dwarf 0.2 x size x 0.7 above its floor (10).
    dwarf.position = glm::vec3(5.0f, 5.0f, 10.0f + 0.2f * 4.0f * 0.7f);
    w.spawns[1] = dwarf;
    g::Spawn wisp = dwarf; // flying: only kept out of the floor
    wisp.fly_mode = 1;
    wisp.position.z = 14.0f;
    w.spawns[2] = wisp;
    g::Spawn sunk = wisp; // flying, its feet in the floor: raised
    sunk.position.z = 11.0f;
    w.spawns[3] = sunk;
    g::Spawn high = dwarf; // on the ground, the zone floor far below (an object under it): left alone
    high.position.z = 30.0f;
    w.spawns[4] = high;
    g::Spawn stale = dwarf; // no update for more than 12 s: not stepped
    stale.update_time = -20.0;
    w.spawns[5] = stale;
    w.smooth(0.016f, 2.0, [](const glm::vec3&) { return std::optional<float>(10.0f); });
    CHECK(w.spawns[1].position.z == 13.0f);
    CHECK(w.spawns[2].position.z == 14.0f);
    CHECK(w.spawns[3].position.z == 13.0f);
    CHECK(w.spawns[4].position.z == 30.0f);
    CHECK(w.spawns[5].position.z == stale.position.z);
}

TEST_CASE("spawn heights: ground_npcs draws an NPC the server left in the air on the floor") {
    g::WorldState w;
    g::Spawn npc;
    npc.is_npc = true;
    npc.heights.avatar_height = 3.0f;
    npc.update_time = 1.0;
    npc.position = glm::vec3(5.0f, 5.0f, 30.0f); // 17 over its stand height (floor 10 + 3)
    w.spawns[1] = npc;
    g::Spawn flier = npc; // flying: left in the air
    flier.fly_mode = 1;
    w.spawns[2] = flier;
    g::Spawn far = npc; // more than 50 over the floor: the server more likely means it
    far.position.z = 70.0f;
    w.spawns[3] = far;
    g::Spawn pc = npc; // another player: its own client places it
    pc.is_npc = false;
    w.spawns[4] = pc;
    g::Spawn stale = npc; // no update for more than 12 s: grounded too
    stale.update_time = -20.0;
    w.spawns[5] = stale;
    g::Spawn fish = npc; // in water: left where it swims
    fish.position.x = 50.0f;
    w.spawns[6] = fish;
    g::Spawn idle = npc; // 3 over, no update for more than 12 s: the landing check never settles it
    idle.update_time = -20.0;
    idle.position.z = 16.0f;
    w.spawns[7] = idle;
    g::Spawn swimmer = npc; // EQEmu's flymode 3 (may swim), out of the water: grounded
    swimmer.fly_mode = 3;
    w.spawns[9] = swimmer;
    g::Spawn wet_swimmer = fish; // the same in the water: left where it swims
    wet_swimmer.fly_mode = 3;
    w.spawns[10] = wet_swimmer;
    g::Spawn close = idle; // within the slack (and not settled, as above): as it is
    close.position.z = 13.2f;
    w.spawns[8] = close;
    const auto floor = [](const glm::vec3&) { return std::optional<float>(10.0f); };
    const auto region = [](const glm::vec3& p) { return static_cast<std::uint8_t>(p.x > 40.0f ? 5 : 0); };

    w.smooth(0.016f, 2.0, floor, region); // off: as the server put them
    for (std::uint32_t id = 1; id <= 10; ++id) CHECK(w.spawns[id].display_position.z == w.spawns[id].position.z);

    w.ground_npcs = true;
    w.smooth(0.016f, 2.0, floor, region);
    w.smooth(10.0f, 2.0, floor, region); // eased all the way
    CHECK(w.spawns[1].position.z == 30.0f); // the position the rules use stays the server's
    CHECK(w.spawns[1].display_position.z == doctest::Approx(13.0f));
    CHECK(w.spawns[2].display_position.z == doctest::Approx(30.0f));
    CHECK(w.spawns[3].display_position.z == doctest::Approx(70.0f));
    CHECK(w.spawns[4].display_position.z == doctest::Approx(30.0f));
    CHECK(w.spawns[5].display_position.z == doctest::Approx(13.0f));
    CHECK(w.spawns[6].display_position.z == doctest::Approx(30.0f));
    CHECK(w.spawns[7].position.z == 16.0f);
    CHECK(w.spawns[7].display_position.z == doctest::Approx(13.0f));
    CHECK(w.spawns[8].display_position.z == doctest::Approx(13.2f));
    CHECK(w.spawns[9].display_position.z == doctest::Approx(13.0f));
    CHECK(w.spawns[10].display_position.z == doctest::Approx(30.0f));

    w.ground_npcs = false; // back where the server put it
    w.smooth(10.0f, 2.0, floor, region);
    CHECK(w.spawns[1].display_position.z == doctest::Approx(30.0f));
    CHECK(w.spawns[1].ground_drop == 0.0f);
}

TEST_CASE("spawn heights: ground_npcs measures a still NPC again once the zone's floor is there") {
    g::WorldState w;
    w.ground_npcs = true;
    g::Spawn npc;
    npc.is_npc = true;
    npc.fly_mode = 3;
    npc.heights.avatar_height = 0.41f;
    npc.update_time = -20.0; // standing still: never stepped, never settled
    npc.position = glm::vec3(-553.0f, 910.0f, -46.1f);
    w.spawns[59] = npc;
    // Before the zone is in: no floor under it (character select's scene), then a wrong one.
    w.smooth(0.016f, 2.0, [](const glm::vec3&) { return std::optional<float>(); });
    CHECK(w.spawns[59].display_position.z == doctest::Approx(-46.1f));
    w.smooth(0.016f, 2.0, [](const glm::vec3&) { return std::optional<float>(-200.0f); });
    CHECK(w.spawns[59].ground_drop == 0.0f); // more than 50 down: left alone, and remembered
    // The zone's floor: measured again once reground says it changed.
    const auto zone_floor = [](const glm::vec3&) { return std::optional<float>(-55.34f); };
    w.smooth(0.016f, 2.0, zone_floor);
    CHECK(w.spawns[59].ground_drop == 0.0f);
    w.reground();
    w.smooth(10.0f, 2.0, zone_floor);
    CHECK(w.spawns[59].ground_drop == doctest::Approx(8.83f).epsilon(0.001));
    CHECK(w.spawns[59].display_position.z == doctest::Approx(-54.93f));
}

TEST_CASE("spawn heights: a race whose model is not loaded becomes a Human, except a mount race") {
    g::WorldState w;
    g::Spawn a;
    a.race = 400;
    a.gender = 1;
    w.spawns[1] = a;
    g::Spawn horse = a;
    horse.race = 216;
    w.spawns[2] = horse;
    w.make_actors([](const g::Spawn&) { return std::optional(g::WorldState::ActorModel{false, true}); });
    CHECK(w.spawns[1].race == 1);
    CHECK(w.spawns[1].gender == 1);
    CHECK(w.spawns[2].race == 216);
    CHECK(g::is_mount_race(732));
    CHECK_FALSE(g::is_mount_race(1));
}

TEST_CASE("elemental swap: UseLuclinElementals maps race 75 by texture to the typed elementals and back") {
    // CreatePlayerActor 0x48f5bc..0x48f771: on, 0 earth 209, 1 fire 212, 2 water 211, any other air 210.
    auto on = [](std::uint8_t tex) { return g::luclin_elemental_swap(75, tex, true); };
    CHECK(on(0)->race == 209);
    CHECK(on(1)->race == 212);
    CHECK(on(2)->race == 211);
    CHECK(on(3)->race == 210);
    CHECK(on(0xff)->race == 210);
    CHECK(on(1)->texture == 0);
    CHECK_FALSE(g::luclin_elemental_swap(209, 0, true));
    CHECK_FALSE(g::luclin_elemental_swap(1, 0, true));
    // Off: the typed ones become race 75 with the matching skin.
    CHECK(g::luclin_elemental_swap(209, 0, false)->texture == 0);
    CHECK(g::luclin_elemental_swap(212, 0, false)->texture == 1);
    CHECK(g::luclin_elemental_swap(211, 0, false)->texture == 2);
    CHECK(g::luclin_elemental_swap(210, 0, false)->texture == 3);
    CHECK(g::luclin_elemental_swap(210, 0, false)->race == 75);
    CHECK_FALSE(g::luclin_elemental_swap(75, 1, false));
    // GetEQClientIniBool: T, t or 1 is true; missing gives the default; empty is false.
    CHECK(g::eqclient_ini_bool(std::nullopt, true));
    CHECK(g::eqclient_ini_bool(std::string("true"), false));
    CHECK(g::eqclient_ini_bool(std::string("1"), false));
    CHECK_FALSE(g::eqclient_ini_bool(std::string("FALSE"), true));
    CHECK_FALSE(g::eqclient_ini_bool(std::string(""), true));
}

TEST_CASE("elemental swap: make_actors draws a mage pet as the typed elemental, or ELE when that is not loaded") {
    g::WorldState w;
    g::Spawn pet;
    pet.race = 75;
    pet.gender = 2;
    pet.appearance.texture = 1; // fire
    pet.appearance.helm = 3;
    w.spawns[1] = pet;
    std::vector<std::uint16_t> asked;
    w.make_actors([&](const g::Spawn& s) {
        asked.push_back(s.race);
        return std::optional(g::WorldState::ActorModel{false, false});
    });
    CHECK(asked == std::vector<std::uint16_t>{212});
    CHECK(w.spawns[1].race == 212);
    CHECK(w.spawns[1].appearance.texture == 0);
    CHECK(w.spawns[1].appearance.helm == 0);
    CHECK(w.spawns[1].actor_made);

    // The typed model missing: back to race 75 with its skin.
    w.spawns[2] = pet;
    asked.clear();
    w.make_actors([&](const g::Spawn& s) {
        asked.push_back(s.race);
        return std::optional(g::WorldState::ActorModel{false, s.race != 75});
    });
    CHECK(asked == std::vector<std::uint16_t>{212, 75});
    CHECK(w.spawns[2].race == 75);
    CHECK(w.spawns[2].appearance.texture == 1);
    CHECK(w.spawns[2].appearance.helm == 3);
    CHECK_FALSE(w.spawns[2].human_actor);

    // No actor can be made yet: nothing changes.
    w.spawns[3] = pet;
    w.make_actors([](const g::Spawn&) { return std::optional<g::WorldState::ActorModel>(); });
    CHECK(w.spawns[3].race == 75);
    CHECK(w.spawns[3].appearance.texture == 1);
    CHECK_FALSE(w.spawns[3].actor_made);

    // UseLuclinElementals off: a typed elemental is drawn as ELE.
    g::WorldState off;
    off.luclin_elementals = false;
    g::Spawn water = pet;
    water.race = 211;
    water.appearance.texture = 0;
    off.spawns[1] = water;
    off.make_actors([](const g::Spawn&) { return std::optional(g::WorldState::ActorModel{false, false}); });
    CHECK(off.spawns[1].race == 75);
    CHECK(off.spawns[1].appearance.texture == 2);
    CHECK(off.spawns[1].appearance.helm == 3);
}
