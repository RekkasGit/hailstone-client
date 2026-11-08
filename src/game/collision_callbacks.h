#pragma once

// The movement sweep's actor callbacks (rof2decomp batch 250,
// re/src/game/spawn/collision_sweep.cpp; checked against the original's
// vectors in tests/movement_vectors_test.cpp): which actors a moving spawn's
// sweep tests, and what an actor hit does to the player.
//
// Actor types (the actor's vf30, eqlib EActorType): 0 Undefined, 1 Player,
// 2 Corpse, 3 Switch, 4 Missile, 5 Object, 6 Ladder, 7 Tree, 8 Wall,
// 9 PlacedObject.

#include <cstdint>
#include <optional>

#include "game/mechanics/fpu.h"

namespace hailstone::game {

inline constexpr std::int32_t kActorPlayer = 1;
inline constexpr std::int32_t kActorSwitch = 3;
inline constexpr std::int32_t kActorMissile = 4;
inline constexpr std::int32_t kActorLadder = 6;
inline constexpr std::int32_t kActorPlacedObject = 9;

// What CollisionFilter_Movement @0x4760c0 / CollisionFilter_MovementWithRider
// @0x476180 ask about an actor in the sweep's way.
struct FilterActor {
    bool has_owner = false;      // the actor's vf20
    std::int32_t type = 0;       // its vf30
    bool spawn_found = false;    // type 1: the owner's vfc -> vf44 (the PlayerClient)
    bool spawn_is_corpse = false; // PlayerClient::IsCorpse
    std::uint8_t spawn_type = 0; // PlayerBase +0x125 (1: NPC)
    bool spawn_vfa8 = false;
    bool spawn_vf50 = false;     // PlayerClient vf50 (a vehicle race)
    bool switch_found = false;   // type 3: the owner's vf1c (EQSwitch)
    std::uint8_t switch_type = 0; // EQSwitch +0x26
    bool object9_found = false;  // type 9: the owner's vf24
    std::uint8_t object9_98 = 0; // its +0x98
    // WithRider only: the mover's vfc, and whether the spawn is the mover's rider.
    bool mover_found = false;
    bool spawn_is_movers_rider = false;
};

// 1: test the actor's geometry, 0: skip it. Spawns block unless they are
// corpses or NPCs whose vfa8 and vf50 are both 0 (with a rider: the rider
// itself is skipped); switches of type 0x32 or 0x33 never block; a type-9
// object whose +0x98 is set does not; everything else does.
std::uint32_t collision_filter_movement(const FilterActor& a, bool with_rider);

// What CollisionHit_SetCollidingType @0x475e70 is told about an actor hit.
struct HitActor {
    std::uint32_t flags = 0;       // the actor's vf54 (bit 2: ignored)
    bool has_owner = false;        // vf20
    std::int32_t type = 0;         // vf30
    bool is_camera_actor = false;  // pinstCamActor
    bool collisions_enabled = false; // byte 0xddf671
    std::int32_t kind = 0;         // the sweep's kind (1: the player's own)
    bool player_found = false;     // kind 1 and the mover's vfc -> vf44
    bool door_has_switch = false;  // the owner's vf1c
};

struct HitResult {
    std::uint32_t ret = 0;                  // 1: the sweep keeps this hit
    std::optional<std::int32_t> colliding_type; // the player's +0x24, when written
    bool asked_touch = false;               // 0x510210 (the switch acts on touch) was asked
    bool touching_switch = false;           // +0x2d4 = the switch and PlayerClient::TouchingSwitch
};

// `switch_acts_on_touch` answers 0x510210 (switch types 0x33, 0x73,
// 0x74, 0x78, 0x7d, 0x82, 0x87, 0x8c, 0x91, 0x92, 0x96..0x98).
HitResult collision_hit_set_colliding_type(const HitActor& a, bool switch_acts_on_touch);

// 0x510210's switch types.
bool switch_acts_on_touch(std::uint8_t switch_type);

} // namespace hailstone::game

namespace hailstone::game {

// What FloorActorFilter @0x476280 asks about an actor in the floor sweep's way
// (rof2decomp batch 251, notes/lifts.md). vf30 is asked once or twice: the
// first answer is `type`, later ones `type_again`.
struct FloorFilterActor {
    bool mover_present = false;   // the sweep's owner (the spawn)
    bool owner_is_mover = false;  // actor vf20 == the mover
    bool has_owner = false;       // actor vf20
    std::int32_t type = 0;        // vf30, first call
    std::int32_t type_again = 0;  // vf30, later calls
    bool mover_vf24 = false;
    std::uint8_t restriction_mask = 0; // vf54 (al; bit 0 skips)
    std::int32_t volume_type = 0;      // vf3c
    double volume_radius = 0.0;        // vf44, unrounded
    bool mover_spawn_found = false;    // the mover's vfc's vf3c
    bool is_own_actor = false;         // that spawn's +0x101c
    bool is_mount_actor = false;       // its mount's vf4c
    bool is_rider_actor = false;       // its rider's vf4c
};
// 1: the floor sweep tests the actor; `is_floor_switch` answers EQSwitch_IsFloorSwitch for a Switch actor's
// switch (asked only then; `asked` tells whether it was).
std::uint32_t floor_actor_filter(const FloorFilterActor& a, bool is_floor_switch, bool* asked = nullptr);

// What CollsnCallbackForFloor @0x475f40 is told about a floor sweep hit.
struct FloorHit {
    bool actor_null = false;
    bool is_camera_actor = false;
    bool player_found = false;     // kind 1 and the owner's vfc's vf44
    bool actor_is_player = false;
    bool has_owner = false;        // actor vf20
    std::int32_t type = 0;         // vf30, first call
    std::int32_t type_again = 0;   // vf30, second call
    bool switch_present = false;   // the owner's vf1c
    std::uint8_t switch_type = 0;  // EQSwitch +0x26
    std::int32_t zone = 0;         // unk_CurrentZoneID
    float player_height = 0.0f;    // PlayerClient::Height (+0x13c)
    float distance = 0.0f;         // the hit's distance along the sweep
    bool hit_spawn_is_player = false; // the owner's vfc's vf44 is the player
    bool hit_spawn_rideable = false;  // that spawn's vf50
};
struct FloorHitResult {
    std::uint32_t ret = 0;
    bool touching_switch = false; // pTouchingSwitch (+0x2d4) = the switch
    bool set_vehicle = false;     // 0x8cfd10(player, the hit spawn): Vehicle (+0x150)
};
FloorHitResult collsn_callback_for_floor(const FloorHit& h, const mechanics::Fpu& fpu = mechanics::Fpu{});

} // namespace hailstone::game
