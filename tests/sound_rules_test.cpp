#include <doctest/doctest.h>

#include "game/sound_rules.h"

using namespace hailstone;
using namespace hailstone::game;

namespace {

std::unordered_map<std::int32_t, assets::RaceSoundIds> human() {
    // racedata.txt's race 1: male 0 0 0 32..40 3001 3002 98 130 129 0, female the same with 42..50.
    assets::RaceSoundIds r{};
    std::int32_t male[18] = {0, 0, 0, 32, 33, 34, 35, 36, 37, 38, 39, 40, 3001, 3002, 98, 130, 129, 0};
    std::int32_t female[18] = {0, 0, 0, 42, 43, 44, 45, 46, 47, 48, 49, 50, 3001, 3002, 98, 130, 129, 0};
    for (int i = 0; i < 18; ++i) {
        r.male[static_cast<std::size_t>(i)] = male[i];
        r.female[static_cast<std::size_t>(i)] = female[i];
    }
    return {{1, r}};
}

} // namespace

TEST_CASE("Spawn sounds: racedata ids, defaults and borrowed races") {
    auto races = human();
    SpawnSounds m = spawn_sounds(races, 1, 0);
    CHECK(m[SoundKind::Jump] == 32);
    CHECK(m[SoundKind::Hit1] == 33);
    CHECK(m[SoundKind::Death] == 39);
    CHECK(m[SoundKind::Walk] == 3001);
    CHECK(m[SoundKind::Attack1] == 130);
    CHECK(m[SoundKind::Swim] == 0x66);           // default constants
    CHECK(m[SoundKind::PrimaryStab] == 0x7a);
    CHECK(m[SoundKind::PrimaryBlunt] == 130);    // default: Attack1's id
    CHECK(m[SoundKind::JumpAcross] == 32);       // default: Jump's id
    CHECK(m[SoundKind::WalkBackwards] == 3001);  // default: Walk's id
    CHECK(m[SoundKind::CrouchWalk] == 98);
    CHECK(m[SoundKind::SecondaryPunch] == 0);    // the Punch id passed as a kind: out of range
    SpawnSounds f = spawn_sounds(races, 1, 1);
    CHECK(f[SoundKind::Jump] == 42);
    CHECK(spawn_sounds(races, 44, 0)[SoundKind::Jump] == 32); // race 44 uses race 1's sounds
    SpawnSounds none = spawn_sounds(races, 900, 0);
    CHECK(none[SoundKind::Jump] == 0);
    CHECK(none[SoundKind::Kick] == 0x7e);
}

TEST_CASE("Door sounds by switch type and state") {
    CHECK(door_sounds(0, 2) == std::vector<std::int32_t>{0x87}); // wooden door opens: DoorWd_O
    CHECK(door_sounds(0, 3) == std::vector<std::int32_t>{0x88}); // closes: DoorWd_C
    CHECK(door_sounds(0, 0).empty());
    CHECK(door_sounds(0x3c, 3) == std::vector<std::int32_t>{0xb5});
    CHECK(door_sounds(0x3e, 0) == std::vector<std::int32_t>{0xb6});
    CHECK(door_sounds(0x73, 3) == std::vector<std::int32_t>{0x77});
    CHECK(door_sounds(0x99, 2).empty());
}

TEST_CASE("Hit, block and miss sounds") {
    auto races = human();
    SpawnSounds h = spawn_sounds(races, 1, 0);
    HitInput in;
    in.damage = 10;
    in.defender_level = 10; // 300 / 10 = 30 -> FlshHit1
    CHECK(hit_sounds(h, in, 2) == std::vector<std::int32_t>{0x93, 34});
    in.damage = 40;         // 300 / 40 = 7 -> FlshHit2
    CHECK(hit_sounds(h, in, 1) == std::vector<std::int32_t>{0x94, 33});
    in.defender_is_me = true;
    in.defender_hp = 1000;  // 1000 / 40 = 25
    CHECK(hit_sounds(h, in, 4).front() == 0x93);
    in.environment = true; // an environmental hit: no FlshHit, the Hit1..4 alone
    CHECK(hit_sounds(h, in, 1) == std::vector<std::int32_t>{33});
    in.environment = false;
    in.defender_corpse = true;
    CHECK(hit_sounds(h, in, 1).empty());
    in.defender_corpse = false;
    in.damage = 0;
    CHECK(hit_sounds(h, in, 1).empty());
    CHECK(block_sound(0, 0, 0, 0) == 0x84);
    CHECK(block_sound(2, 2, 0, 1) == 0x78);
    CHECK(block_sound(2, 2, 3, 0) == 0x7d);
    CHECK(miss_sound(-4) == 0x73);
    CHECK_FALSE(miss_sound(-2).has_value());
}

TEST_CASE("Animation sounds: footsteps every 500 ms, jumps, attacks, loops of others") {
    auto races = human();
    SpawnSounds h = spawn_sounds(races, 1, 0);
    AnimationSoundState st;
    AnimationSoundInput in;
    in.local = true;
    in.race = 1;
    int calls = 0;
    auto rnd = [&](std::int32_t low, std::int32_t) { ++calls; return low; };

    in.action = 0x12; // run
    auto a = animation_sounds(h, in, st, 1000, rnd);
    REQUIRE(a.play.size() == 1);
    CHECK(a.play[0] == 0x5f); // StepRunR (random 0)
    CHECK(a.loop == 0);        // animation changed: back to the Loop3d sound
    CHECK(animation_sounds(h, in, st, 1400, rnd).play.empty()); // within 500 ms
    CHECK(animation_sounds(h, in, st, 1501, rnd).play.size() == 1);
    in.sneaking = true;
    CHECK(animation_sounds(h, in, st, 3000, rnd).play.empty());
    in.sneaking = false;

    in.action = 0x13; // jump
    a = animation_sounds(h, in, st, 4000, rnd);
    CHECK(a.play == std::vector<std::int32_t>{32});

    in.action = 5;    // one-handed weapon, holding something: Swing then Attack1
    in.right_holding = 1;
    a = animation_sounds(h, in, st, 5000, rnd);
    CHECK(a.play == std::vector<std::int32_t>{0x76}); // Attack1 is Punch1 (0x82), which the original drops here
    in.right_holding = 0; // bare hands: Attack1 (local always)
    a = animation_sounds(h, in, st, 5100, rnd);
    CHECK(a.play == std::vector<std::int32_t>{130});

    // Another spawn walking: its walk sound becomes its loop sound, nothing is played.
    AnimationSoundState other;
    AnimationSoundInput o;
    o.race = 1;
    o.action = 0x11;
    a = animation_sounds(h, o, other, 6000, rnd);
    CHECK(a.play.empty());
    CHECK(a.loop == 3001);

    // Race 0x7f makes no animation sounds.
    in.race = 0x7f;
    in.action = 4;
    CHECK(animation_sounds(h, in, st, 7000, rnd).play.empty());
}

TEST_CASE("Held item kinds (SetHeldModel's RightHolding)") {
    CHECK(held_type(0) == 0);
    CHECK(held_type(1) == 2);
    CHECK(held_type(4) == 4);
    CHECK(held_type(6) == 1);
    CHECK(held_type(61) == 5);
    CHECK(held_type(200) == 3);
    CHECK(held_type(301) == 1);
    CHECK(held_type(10778) == 5);
}

TEST_CASE("Landing sounds") {
    CHECK(landing_sounds(false, std::nullopt).empty());
    CHECK(landing_sounds(true, std::nullopt) == std::vector<std::int32_t>{kSoundJumpLand});
    CHECK(landing_sounds(false, 0) == std::vector<std::int32_t>{kSoundJumpLand});
    CHECK(landing_sounds(true, 25) == std::vector<std::int32_t>{kSoundJumpLand, kSoundFallHurt});
}
