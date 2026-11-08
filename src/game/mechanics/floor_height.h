#pragma once

// CDisplay::GetPlayerFloorHeight @0x507230 (a thiscall on the spawn): the
// floor under a spawn, cached. Exact port of rof2decomp batch 251
// (re/src/game/world/lift_floor.cpp), checked against the original's vectors
// (tests/movement_vectors_test.cpp). The cache (+0x1060 CollisionCounter,
// +0x1064 CachedFloorLocation, +0x1070 CachedFloorHeight) holds while x and y
// are unchanged, z + 0.001 is not below the cached z, (for the controlled
// player) the cached ceiling - 2r is not below z and there is no Vehicle;
// a counter above 2 (DoCollisionMovement counts it up) returns the cached
// floor, else a sphere of radius AvatarHeight x 0.1 (0.1 when that is 0) is
// swept from the position down to -32767 (s3dCollideSphereWithWorld with
// FloorActorFilter and CollsnCallbackForFloor) and the floor is the stop
// point's z - r + 0.001 (-1e27 below the zone's lowest floor). ProcessSwitches
// zeroes the counter of the spawns near a moving floor switch: that is how a
// lift's riders follow it (game/mechanics/switches.h).

#include <array>
#include <cstdint>

#include "game/mechanics/fpu.h"

namespace hailstone::game::mechanics {

inline constexpr float kNoFloorHeight = -1e27f; // 0x9c5760

// The spawn fields the floor test reads and writes.
struct FloorCache {
    std::uint32_t collision_counter = 0;   // +0x1060
    std::array<float, 3> location{};       // +0x1064..+0x106c
    float floor = 0.0f;                    // +0x1070
    std::array<float, 3> ceiling_location{}; // +0x1074..+0x107c CachedCeilingLocation (CanIFitHere)
    float ceiling = 0.0f;                  // +0x1080 CachedCeilingHeight
    bool touching_switch = false;          // +0x2d4 is set (cleared by a sweep with `clear`)
};

// What the floor test asks and tells.
class FloorQueries {
public:
    virtual ~FloorQueries() = default;
    virtual bool is_3d() = 0;                              // CDisplay::is3dON
    virtual bool has_vehicle() = 0;                        // PlayerClient +0x150
    virtual bool scene_accepts(const std::array<float, 3>& p) = 0; // pinstSceneGraph vf4c
    virtual void set_vehicle_none() = 0;                   // FUN_008cfd10(0)
    virtual void touching_cleared() {}                     // `clear`: pTouchingSwitch = 0
    // s3dCollideSphereWithWorld(from, to, radius, FloorActorFilter, CollsnCallbackForFloor, ...); `actor_normal`:
    // the spawn's actor gets the hit's normal (`clear`). Returns its result (0 in practice) and the stop point's z.
    virtual std::uint32_t sweep(const std::array<float, 3>& from, const std::array<float, 3>& to, float radius,
                                bool actor_normal, float& hit_z) = 0;
};

struct FloorWorld {
    bool controlled = false;         // the spawn is pinstControlledPlayer
    float underworld = -40000.0f;    // float 0xddfb50: the zone's lowest floor
    bool below_allowed = false;      // dword 0xddfc54
};

// Returns the floor, unrounded as the original leaves it on the FPU.
double get_player_floor_height(FloorCache& cache, const std::array<float, 3>& position, bool clear,
                               float avatar_height, const FloorWorld& world, FloorQueries& q, const Fpu& fpu = Fpu{});

} // namespace hailstone::game::mechanics
