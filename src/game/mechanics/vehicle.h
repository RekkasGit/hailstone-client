#pragma once

// Riding a vehicle (a boat, or any spawn whose race is a vehicle:
// PlayerClient_IsVehicleRace, vtable +0x50). A spawn stands on a vehicle when
// its +0x150 points at it. Around each physics step of a vehicle the original
// moves every player standing on it into the vehicle's frame
// (Vehicle_BeforeStep @0x8d2220) and back into the world after it
// (Vehicle_AfterStep @0x8d22e0), so passengers keep their place on the deck
// while it moves and turns. The position a player sends while on a vehicle is
// in the vehicle's frame too (PackPlayerPositionUpdateOnVehicle @0x4fa650).
//
// Exact ports of rof2decomp batch 252 (re/src/game/spawn/vehicle_riders.cpp),
// checked against Vehicle_WorldToLocal.jsonl / Vehicle_LocalToWorld.jsonl at
// both x87 precisions; rof2decomp notes/combat-and-movement.md §3.5.

#include <array>

#include "game/mechanics/fpu.h"

namespace nmsc::game::mechanics {

// The parts of a physics block (spawn +0x64) the conversions touch.
struct VehicleBody {
    float x = 0, y = 0, z = 0;                   // +0, +4, +8
    float speed_x = 0, speed_y = 0, speed_z = 0; // +0xc..+0x14
    float heading = 0, pitch = 0;                // +0x1c, +0x20
};

// The vehicle's own position (+0x64..+0x6c), heading (+0x80) and pitch (+0x84).
struct VehiclePose {
    float x = 0, y = 0, z = 0;
    float heading = 0, pitch = 0;
};

// The 3x3 frame, row-major: CMath vtable +0x1c (EQGraphicsDX9 0x100b9910) of
// heading h and pitch p, roll 0: rows {cp ch, cp sh, sp}, {-sh, ch, 0},
// {-sp ch, -sp sh, cp} with CMath's interpolated sine and cosine (+0/+4).
using VehicleFrame = std::array<float, 9>;
VehicleFrame vehicle_frame(float heading, float pitch);

// Vehicle_WorldToLocal @0x8cfde0: position = M (p - vehicle), heading and
// pitch less the vehicle's, speed = M v.
void vehicle_world_to_local(const VehiclePose& vehicle, VehicleBody& body, const VehicleFrame& m,
                            const Fpu& fpu = Fpu{});
// Vehicle_LocalToWorld @0x8cfea0: position = M^T p + vehicle, heading and
// pitch plus the vehicle's through __FixHeading, speed = M^T v.
void vehicle_local_to_world(const VehiclePose& vehicle, VehicleBody& body, const VehicleFrame& m,
                            const Fpu& fpu = Fpu{});

// Vehicle_ToLocal @0x8d00e0 / Vehicle_ToWorld @0x8d0130: the same with the
// vehicle's own frame.
inline void vehicle_to_local(const VehiclePose& v, VehicleBody& b, const Fpu& f = Fpu{}) {
    vehicle_world_to_local(v, b, vehicle_frame(v.heading, v.pitch), f);
}
inline void vehicle_to_world(const VehiclePose& v, VehicleBody& b, const Fpu& f = Fpu{}) {
    vehicle_local_to_world(v, b, vehicle_frame(v.heading, v.pitch), f);
}

// PhysicsObject_StopCorpseSlide @0x8d0830: a corpse (spawn type 2 or 3) gets
// no sideways speed in Step.
inline void stop_corpse_slide(std::uint8_t spawn_type, float& speed_x, float& speed_y) {
    if (spawn_type == 2 || spawn_type == 3) speed_x = speed_y = 0.0f;
}

} // namespace nmsc::game::mechanics
