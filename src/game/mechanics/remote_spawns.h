#pragma once

// How the original client moves other spawns: a position update
// (OP_ClientUpdate, also OP_NPCMoveUpdate) is unpacked straight into the
// spawn's physics block, snapping its position and heading, and every frame
// PhysicsObject::Step and CollisionPass run for each other spawn whose last
// update is at most 12 s old.
//
// Follow the original exactly:
// unpack_physics @0x4ee6c0, Handle_OP_ClientUpdate @0x4b3d00 and
// 0x4877e0 (AdvanceSpawn), checked against the original's
// vectors (tests/movement_vectors_test.cpp).

#include <array>
#include <cstdint>
#include <span>

#include <glm/glm.hpp>

#include "game/mechanics/fpu.h"

namespace hailstone::game::mechanics {

// A spawn's physics block (PlayerBase +0x64..), the fields an update sets,
// with the original's Y/X naming (game/coords.h swaps them for the world).
struct SpawnPhysics {
    float y = 0.0f, x = 0.0f, z = 0.0f;                   // +0..+8
    float speed_y = 0.0f, speed_x = 0.0f, speed_z = 0.0f; // +0xc..+0x14: units per physics tick
    float speed_run = 0.0f;      // +0x18: the animation field x 0.025
    float heading = 0.0f;        // +0x1c, 512ths (signed: -256..256)
    float speed_heading = 0.0f;  // +0x28: the turn rate
    float pitch = 0.0f;          // +0x2c CameraAngle
};

// The five packed dwords after the u16 spawn id and u16 vehicle id.
using PackedPosition = std::array<std::uint32_t, 5>;

// What the unpack does with the z (only with a spawn).
struct UnpackZ {
    bool spawn = true;          // a spawn: without one the z is not stored
    bool levitating = false;    // its physics mode (vf78 +0xc) is 2 or 5: z follows only moves of more than 5
    bool keep_level = false;    // the caller's flag (Handle_OP_ClientUpdate passes 0)
    bool vf54 = false;          // PlayerClient +0x54: the old z is restored and vz cleared
    bool keep_z_global = false; // 0xdcf224: with vf54 only vz is cleared
};

// unpack_physics @0x4ee6c0: each field is the signed integer times its unit
// (0.125 positions, 1/64 deltas, 0.25 heading and pitch, 0.025 SpeedRun,
// 0.05 the turn rate), rounded to float. A vehicle's transform (0x8d0130)
// is the caller's.
void unpack_spawn_position(SpawnPhysics& physics, const PackedPosition& packed, const UnpackZ& z,
                           const Fpu& fpu = Fpu{});

// Animation 66 in both the animation and the turn-rate fields: a stop
// (Handle_OP_ClientUpdate clears both and the deltas before the unpack, and
// sets the spawn's motion state +0x270 to 6). Returns whether it was one.
bool clear_stopped_motion(PackedPosition& packed);

// Handle_OP_NPCMoveUpdate @0x4b3ef0: the bit stream
// unpacked into the 0x18-byte OP_ClientUpdate record that it hands to
// Handle_OP_ClientUpdate. Fields are read high bit first (BitStream::ReadUInt
// @0x80d380); the signed ones (BitStream::ReadInt @0x80d460) are a sign bit
// then n-1 bits of magnitude. A value that runs past the end reads as 0.
// Layout: spawn id and vehicle id (16 bits each), six flag bits, y, x and z
// (19 bits), the heading (12), then by flag: 1 pitch (12), 2 animation (10),
// 4 the turn rate (10), 8 delta z (13), 0x10 delta x (13), 0x20 delta y (13).
// record[0] is the two ids; record[k + 1] is PackedPosition[k].
std::array<std::uint32_t, 6> unpack_npc_move_update(std::span<const std::uint8_t> data);

// Handle_OP_ClientUpdate @0x4b3d00's decisions for one update.
struct ClientUpdateInput {
    bool found = false;        // GetSpawnByID found the spawn
    bool has_mount = false;    // the spawn rides something (EQPlayer::GetMount)
    bool has_rider = false;    // something rides the spawn (PlayerBase_GetRider)
    bool in_game = false;      // CEverQuest +0x5c8 is 5
    bool local_player = false; // pinstLocalPlayer exists
    std::uint32_t now = 0;     // CDisplay +0x154
};
struct ClientUpdateResult {
    bool handled = false;        // returns 1
    bool request_spawn = false;  // OP 0x43a3 for the unknown id (and the local player's +0x268 stamped)
    bool unpack_rider = false;   // the spawn's own physics first (it rides): then its mount takes the update
    bool unpack = false;         // the update goes into the target (the spawn, or the mount it rides)
    bool stop = false;           // animation 66 in both fields: motion state 6, animation and delta bits cleared
};
// `last_request` is the local player's +0x268 (the last spawn request);
// `packed` is changed by the animation-66 stop as the original does.
ClientUpdateResult handle_client_update(const ClientUpdateInput& in, PackedPosition& packed,
                                        std::uint32_t& last_request);

inline constexpr std::uint32_t kSpawnStaleMs = 12000;    // AdvanceSpawn: no movement after 12 s without an update
inline constexpr std::uint32_t kSpawnRequestMs = 15000;  // at most one unknown-spawn request per 15 s

// 0x4877e0: whether a spawn other than the local and controlled
// players is stepped (PhysicsObject::Step and CollisionPass) this frame.
struct AdvanceInput {
    bool hidden = false;            // +0x1e54 or +0x1e55
    bool is_local = false;          // the spawn is the local player
    bool is_controlled = false;     // ... the controlled player
    bool controlled_by_player = false; // ... what the controlled player controls (+0x150)
    std::uint32_t ms_since_update = 0; // now - +0x11c0 (unsigned)
    bool corpse = false;            // PlayerClient::IsCorpse
    float speed_run = 0.0f;         // +0x7c
    float speed_z = 0.0f;           // +0x78
    bool force_move = false;        // 0xddf790
};
bool advance_spawn(const AdvanceInput& in);

// Another spawn's physics between updates, in world coordinates.
struct RemoteBody {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};   // world units per physics tick (50 ms): the update's deltas, then stepped
    float heading = 0.0f;       // 512ths
    float speed_heading = 0.0f; // heading units per tick (the update's turn rate)
    float speed_run = 0.0f;     // the update's animation field x 0.025
};

// PhysicsObject::Step for another spawn on the ground (the analysis's reading
// of Step): dt = min(elapsed_ms x 0.02, 3);
// the heading turns by speed_heading x dt; the velocity keeps c (0.625, the
// zone's AreaFriction; halved, PhysicsObject vf12 @0x59e030, for an NPC
// without a rider that moves sideways) and gains 0.8 x speed_run along the
// heading (vf3 @0x8d2040, ApplyFriction @0x8d2160); the position moves by
// velocity x dt. So the velocity starts at the update's deltas and settles at
// 0.8 x speed_run / (1 - c) per tick. Ice, gravity and the landing check are
// the caller's (the vertical speed is left alone).
void step_remote_spawn(RemoteBody& body, float elapsed_ms, bool npc);

} // namespace hailstone::game::mechanics
