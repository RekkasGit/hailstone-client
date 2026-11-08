#pragma once

// Climbing onto a clicked ladder: Ladder_MountClickedLadder @0x509c50, run
// when the player clicks a switch whose actor type is 6.
//
// Exact port of rof2decomp batch 252 (re/src/game/spawn/ladder.cpp), checked
// against Ladder_MountClickedLadder.jsonl at both x87 precisions; rof2decomp
// notes/combat-and-movement.md §3.5:
//  - already climbing (movement mode 6): only turn to the ladder's heading,
//    or its opposite when __HeadingDiff >= 256;
//  - A = max(player actor height (vf44), 2) + 0.5;
//  - more than 25 from the ladder's top (2-D) prints 0x2ee8 (too far); feet
//    at or below ladder z + 0.7 height do nothing;
//  - the player stands A from the ladder on its far side (heading + 256)
//    facing the ladder's heading, or else on the near side facing away, at
//    ladder z + height - AvatarHeight (1.5 when AvatarHeight <= 1.5), when it
//    fits (CanIFitHere); else nothing;
//  - then speed (A + 2) along the heading, SpeedRun 0, pitch -75, movement
//    mode 6, not standing on a vehicle (+0x150 0), +0x2d4 0, and the floor
//    from one unit above the feet (unless there is none, -1e27).
// What the original asks other functions (CMath's sine, cosine and root,
// CanIFitHere, GetPlayerFloorHeight) or tells them (the "too far" message,
// the actor update) is a LadderQueries call, in the original's order.

#include <cstdint>

#include "game/mechanics/fpu.h"

namespace hailstone::game::mechanics {

// The clicked switch (its actor's vtable +0x30, +0x80, +0x84, +0x60).
struct LadderSwitch {
    std::int32_t type = 0; // 6: a ladder
    float x = 0, y = 0, z = 0;
    float heading = 0;     // the first of its three angles
    float height = 0;
};

// The player's fields the function reads and writes (spawn offsets).
struct LadderPlayer {
    float x = 0, y = 0, z = 0;    // +0x64..+0x6c
    float heading = 0;            // +0x80
    std::int32_t movement_mode = 0; // +0x24: 6 on a ladder
    float speed_x = 0, speed_y = 0, speed_z = 0; // +0x70..+0x78
    float speed_run = 0;          // +0x7c
    float pitch = 0;              // +0x90
    float floor_height = 0;       // +0x28
    bool standing_on_vehicle = false; // +0x150 non-null
    std::int32_t field_2d4 = 0;   // +0x2d4 (TouchingSwitch)
    float avatar_height = 0;      // +0x138
};

class LadderQueries {
public:
    virtual ~LadderQueries() = default;
    virtual double actor_height() = 0;      // the player actor's (+0x101c) vf44 (unrounded)
    virtual double ladder_height() = 0;     // the ladder actor's vf60 (unrounded)
    virtual double sine(float heading) = 0;   // CMath +0x08 (table, no interpolation)
    virtual double cosine(float heading) = 0; // CMath +0x0c
    virtual double root(float value) = 0;     // CMath +0x10 (CDisplay::TrueDistance's square root)
    virtual void too_far() = 0;               // dsp_chat(string 0x2ee8)
    virtual bool can_i_fit_here(float x, float y, float z) = 0; // PlayerZoneClient::CanIFitHere(pos, 0)
    virtual void update_player_actor() = 0;   // SetCCreateCamera(player)
    virtual double floor_height(float x, float y, float z, std::uint32_t flag) = 0; // CDisplay::GetPlayerFloorHeight
};

inline constexpr std::int32_t kLadderMovementMode = 6;
inline constexpr std::int32_t kLadderSwitchType = 6;
inline constexpr std::uint32_t kStrLadderTooFar = 0x2ee8;

// Returns true when the player was put on the ladder (or turned on it).
bool mount_clicked_ladder(const LadderSwitch& ladder, LadderPlayer& player, LadderQueries& q, const Fpu& fpu = Fpu{});

// __HeadingDiff @0x8d1fc0: |a - b| folded to 0..256, unrounded.
double ladder_heading_diff(float a, float b, const Fpu& fpu = Fpu{});

} // namespace hailstone::game::mechanics
