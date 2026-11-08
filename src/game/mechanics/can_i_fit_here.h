#pragma once

// The room checks around a spawn's move, as the original makes them,
// checked against its vectors
// (tests/movement_vectors_test.cpp):
//
// PlayerZoneClient::CanIFitHere @0x509650: whether the controlled player has
// room at a position. The floor F (GetPlayerFloorHeight; -1e27 refuses unless
// 0xddfc54), the ceiling C (the cached one when CollisionCounter > 2, else an
// upward sphere query of the actor's radius; none fits), the height needed h
// (AvatarHeight + 1 ducking, 3 x AvatarHeight for a ridden mount race, +0x144
// when above 0, else 2 x AvatarHeight): F < C <= F + h + 0.5 refuses (and can
// start a 2 s auto-duck), C - F >= h fits.
//
// 0x506c90: whether a spawn that is not moving must still be collided.

#include <array>
#include <cstdint>
#include <optional>

#include "game/mechanics/floor_height.h"
#include "game/mechanics/fpu.h"

namespace hailstone::game::mechanics {

// The spawn fields CanIFitHere reads (the cache it writes is FloorCache).
struct FitSpawn {
    bool controlled = true;                 // the spawn is pinstControlledPlayer
    bool terrain_collision = true;          // pinstCollision vf44
    std::optional<double> actor_radius;     // the actor's vf44 (unrounded); none: no actor (1.0)
    float avatar_height = 0.0f;             // +0x138
    float special_height = 0.0f;            // +0x144
    std::uint8_t stand_state = 0x64;        // +0x35c
    bool mount_race = false;                // PlayerBase_IsMountRace
    bool has_rider = false;                 // PlayerBase_GetRider
};

// The globals of the auto-duck under a low ceiling.
struct AutoDuck {
    std::uint8_t active = 0;      // byte 0xddf675
    std::uint32_t until = 0;      // dword 0xddf678: CDisplay time before which it does not stand up
    bool allowed = false;         // byte 0xde0d5a
};

class FitQueries {
public:
    virtual ~FitQueries() = default;
    // GetPlayerFloorHeight(position, 0), unrounded (it may update the cache).
    virtual double floor(const std::array<float, 3>& position) = 0;
    // CDisplay::GetCeilingHeight(x, y, z, r, (-1, -1, 1), 0, 0), unrounded; -1e27: none.
    virtual double ceiling(const std::array<float, 3>& position, float radius) = 0;
    virtual void stand_up() = 0;                     // ChangePosition(0x64)
    virtual void set_player_ceiling(float c) = 0;    // the vf44 object's +0x11b4
};

bool can_i_fit_here(FloorCache& cache, const std::array<float, 3>& position, const FitSpawn& spawn,
                    bool below_allowed, AutoDuck& duck, std::uint32_t display_time, FitQueries& q,
                    const Fpu& fpu = Fpu{});

// 0x506c90's inputs.
struct StillSpawn {
    bool jump_animation = false;  // the animation object's vf24: L03/L04 (old models), JMPA/JMPU (new)
    bool controlled = false;      // vf6c
    std::int32_t levitate = 0;    // PlayerPhysics::Levitate (vf78 +0xc)
    std::uint8_t auto_duck = 0;   // byte 0xddf675
    bool vehicle_race = false;    // vf50
    float heading = 0.0f;         // +0x80
    double actor_heading = 0.0;   // the actor's vf88, unrounded
};
bool must_collide_still(const StillSpawn& s);

} // namespace hailstone::game::mechanics
