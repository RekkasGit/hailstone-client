#pragma once

// CDisplay::ProcessSwitches @0x48d530: each frame every door / switch moves
// toward its state's position (doors turn, tilts pitch, lifts and platforms
// slide, gates swing), its actor follows, and the spawns near a moving floor
// switch get their floor cache reset (CollisionCounter = 0), so their next
// floor test finds the switch's new top: that is all a lift does for its
// riders (no horizontal carry, no turn; rof2decomp notes/lifts.md).
//
// Exact port of rof2decomp batch 251 (re/src/game/world/process_switches.cpp),
// checked against the original's vectors (tests/movement_vectors_test.cpp),
// with __FixHeading @0x8d1f50, __HeadingDiff @0x8d1fc0 and
// EQSwitch::GetCustomMoveDistance @0x50ff20. What the original asks or tells
// another function (ChangeState, RepopSwitch, RepopFrequency,
// SlideSwitchLeftRight, the day/night update, particles, the actor, the
// spawns) is a SwitchQueries call.

#include <cstdint>
#include <optional>
#include <span>

#include "game/mechanics/fpu.h"

namespace hailstone::game::mechanics {

// The EQSwitch fields ProcessSwitches reads and writes (offsets in comments).
struct Switch {
    std::uint8_t type = 0;          // +0x26
    std::uint8_t state = 0;         // +0x27: 0 closed, 1 open, 2 opening, 3 closing, 4 triggered
    std::uint8_t default_state = 0; // +0x58
    bool hidden = false;            // +0x5b
    std::uint32_t timestamp = 0;    // +0xac: ms of the last state change
    std::int32_t custom = 0;        // +0x78: the custom move distance / particle id
    float base[3] = {0, 0, 0};      // +0x28..+0x30 (y, x, z)
    float base_heading = 0.0f;      // +0x34
    float base_pitch = 0.0f;        // +0x38 (type 0x35: the day/night mode)
    float spin = 0.0f;              // +0x3c
    float spin_accel = 0.0f;        // +0x40
    float position[3] = {0, 0, 0};  // +0x44..+0x4c
    float heading = 0.0f;           // +0x50
    float pitch = 0.0f;             // +0x54
    float swing_speed = 0.0f;       // +0xb0
    bool swing_stop = false;        // +0xd7
    bool particle = false;          // +0xa8: a running particle effect
};

// A spawn the riders check looks at.
struct SwitchSpawn {
    float y = 0.0f, x = 0.0f;               // +0x64, +0x68
    std::optional<double> actor_radius;     // its actor's vf44 (unrounded); none: 10.0
};

class SwitchQueries {
public:
    virtual ~SwitchQueries() = default;
    // EQSwitch::ChangeState(dword): the state is the low byte (a triggered switch's return passes the switch
    // pointer's upper bytes with it).
    virtual void change_state(Switch& sw, std::uint32_t state) = 0;
    virtual void repop_switch(Switch& sw) = 0;                         // EQSwitch::RepopSwitch @0x512690
    virtual std::uint32_t repop_frequency(const Switch& sw) = 0;       // EQSwitch::RepopFrequency
    // CDisplay::SlideSwitchLeftRight(0.0, flag, dt): al.
    virtual std::uint8_t slide_switch_left_right(Switch& sw, std::uint32_t flag, float dt) = 0;
    virtual void update_day_night(Switch& sw) = 0;                     // CDisplay_UpdateDayNightSwitch
    virtual bool local_player() = 0;                                   // pinstLocalPlayer
    virtual bool start_particle(Switch& sw, std::int32_t id) = 0;      // DoParticleEffect: true when one started
    virtual void stop_particle(Switch& sw) = 0;                        // the effect's vf10(1000)
    virtual bool is_floor_switch(const Switch& sw) = 0;                // EQSwitch_IsFloorSwitch
    virtual std::optional<double> actor_radius(const Switch& sw) = 0;  // the actor (+0xa4) vf44; none: no actor
    virtual void actor_place(Switch& sw, const float position[3], const float orientation[3]) = 0; // vf70, vf74
    virtual std::span<const SwitchSpawn> spawns() = 0;                 // the spawn list, in order
    virtual void reset_collision_counter(std::size_t spawn) = 0;       // PlayerZoneClient_ResetCollisionCounter
    virtual void set_switch_moved(std::size_t spawn) = 0;              // bSwitchMoved (+0x32c) = 1
};

// __FixHeading @0x8d1f50: into [0, 512) by at most six 512 steps (else 0); unrounded.
double fix_heading_x87(float heading, const Fpu& fpu = Fpu{});
// __HeadingDiff @0x8d1fc0: |a - b| folded to 0..256; unrounded.
double heading_diff_x87(float a, float b, const Fpu& fpu = Fpu{});
// EQSwitch::GetCustomMoveDistance @0x50ff20: +0x78 when above 0, -it below -3, else 0.
double custom_move_distance_x87(std::int32_t custom);
// FUN_005101f0: switch types that never move (0x32..0x3a but 0x35, 0x99..0x9b).
bool switch_is_static(std::uint8_t type);

// One ProcessSwitches frame: `elapsed_ms` is __EQGetTime minus the last call's (unsigned), `now_ms` the time
// it stores (the switches' ages are measured from it).
void process_switches(std::span<Switch*> switches, std::uint32_t elapsed_ms, std::uint32_t now_ms, SwitchQueries& q,
                      const Fpu& fpu = Fpu{});

} // namespace hailstone::game::mechanics
