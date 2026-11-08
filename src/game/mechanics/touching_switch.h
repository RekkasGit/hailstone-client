#pragma once

// PlayerClient::TouchingSwitch @0x5939e0: what a spawn does with the switch it touches (+0x2d4, pTouchingSwitch).
// rof2decomp re/src/game/spawn/touching_switch_eh.cpp. Three places set +0x2d4:
//   - the movement sweep's hit callback (CollisionHit_SetCollidingType @0x475e70) for a switch that acts on touch
//     (FUN_00510210), and calls TouchingSwitch at once;
//   - the touch overlap test DoCollisionMovement runs when the spawn does not collide (FUN_00509a00 @0x508787: a
//     sphere of half the actor's radius at the actor, against the touch switches; its hit callback 0x475c50 sets the
//     local player's +0x2d4 and calls TouchingSwitch);
//   - the floor test's callback (CollsnCallbackForFloor @0x475f40) for a floor switch less than Height + 2 below
//     the spawn; then the next PlayerClient::CollisionPass (@0x509050 → 0x509240, before DoCollisionMovement) calls
//     TouchingSwitch for the controlled player (a PC or an NPC, +0x125 <= 1), and clears +0x2d4 for anyone else.
//
// Independent of the network: the caller sends what it decides.

#include <cstdint>
#include <functional>
#include <optional>

namespace hailstone::game::mechanics {

// The switch's fields TouchingSwitch reads.
struct TouchedSwitch {
    std::uint8_t id = 0;          // +0x05 (the last-damage switch is remembered by identity)
    std::uint8_t type = 0;        // +0x26
    std::uint8_t state = 0;       // +0x27
    std::int32_t param = -1;      // +0x78
    std::int32_t adventure_id = 0; // +0xb8 (the record's +0x44)
    std::uint32_t dyn_id = 0;     // +0xc8 (+0x48)
    std::uint8_t has_script = 0;  // +0xcc (+0x50)
    std::uint32_t repop_ms = 0;   // EQSwitch::RepopFrequency
    std::int32_t damage = 0;      // EQSwitch::GetSwitchDamage
};

// The touching spawn's fields.
struct TouchSpawn {
    std::uint16_t spawn_id = 0;          // +0x148
    std::uint8_t spawn_type = 0;         // +0x125 (0: a PC)
    std::uint32_t last_touch_ms = 0;     // +0x3d8
    bool has_inventory = true;           // FUN_0059f730 (a PC's)
    std::optional<std::int32_t> cursor_item_id; // the item on the cursor (slot 0x21), when one is held
};

// What the original calls.
struct TouchHooks {
    std::function<std::int32_t()> random_percent;          // RandomPercentRoll
    // FUN_004dd7a0: the zone (zone | instance << 16) of the zone point numbered `param`, else -1.
    std::function<std::int32_t(std::int32_t param)> zone_point_zone;
    // FUN_004dd6e0 / FUN_004dd740: EQWorldData::GetGeometryNameFromIndex(zone) finds a name and "<name>.s3d" /
    // "<name>.eqg" exists.
    std::function<bool(std::int32_t zone)> zone_s3d_exists;
    std::function<bool(std::int32_t zone)> zone_eqg_exists;
};

struct TouchResult {
    bool clear = true; // +0x2d4 = 0 afterwards (types 0x73 and up keep it while they do nothing)
    // EQSwitch::UseSwitch(switch, user, key, 0, 0).
    struct Use {
        std::uint32_t user = 0;
        std::int32_t key = -1;
    };
    std::optional<Use> use;
    // CharacterZoneClient_TakeEnvironmentalDamage(damage, {spawn, 0xc80000, damage, 1.0, type 0xfd, spell 200,
    // the switch type}, 1, 0, 1, 1) on the spawn's character (+0x2cc).
    std::optional<std::int32_t> damage;
    bool close_now = false; // type 0x32 past its repop time: state (+0x27) = 0, nothing sent
};

inline constexpr std::uint8_t kTrapDamageType = 0xfd;

// `last_damage_switch` is the global 0xe6418c (the switch that last hurt the local player, by id); `now` is
// CDisplay +0x154. FUN_00510210 is game::switch_acts_on_touch (game/collision_callbacks.h); the floor test's
// rule for setting +0x2d4 is game::collsn_callback_for_floor.
TouchResult touching_switch(const TouchedSwitch& sw, TouchSpawn& spawn, std::uint32_t now,
                            std::optional<std::uint8_t>& last_damage_switch, const TouchHooks& hooks);

} // namespace hailstone::game::mechanics
