#pragma once

// Where the sound listener is and which room it is in, as the original computes them (rof2decomp batch 256,
// re/src/game/audio/listener_music.cpp; checked against the original's vectors in tests/audio_vectors_test.cpp):
//
//  - CDisplay::RealRender_World @0x49ca83 passes the local player's position (+0x64 y, +0x68 x, +0x6c z) with the
//    camera's heading (512 units a turn) to EqSoundManager::SetListenerLocation @0x4a94a0, which hands Miles
//    (y, x, -z) and the heading in radians; SoundManager::SetListenerLocation @0x5bdc90 stores them and faces the
//    listener along (cos a, sin a, 0) with up (0, 0, -1).
//  - CDisplay_UpdateListenerEnvironment @0x48a4a0 picks the listener environment from the room height
//    h = ftol(|ceiling (+0x11b4) - floor (+0x28)|): low (3) up to 20, high (2) up to 500 when indoors, else
//    outside (1). Indoors is CDisplay +0x28ec, which CDisplay::SetupEmitterEnvironment @0x48a500 sets when the player
//    actor's scaled ambient is below 0.48: every frame, as its 500 ms gate compares last check - now (unsigned),
//    which only holds back a clock that went backwards. RealRender_World runs it every frame unless a special environment is set
//    (CDisplay +0x2d68): 0x48a480 sets room type 0x16 when the head goes under water (HeadWet 5) and 0x17 when
//    the player is stunned (CharacterZoneClient_ApplyStun); leaving the water and PrepareForRepop run the update
//    again, which clears it.
//  - EqSoundManager::SetListenerEnvironmentLow / High / Outside give the SoundManager the zone's room type for that
//    environment (+0xfe4 / +0xfe0 / +0xfe8: 12 / 2 / 19 unless SetCurrentZone's table at 0xac7b80 names the zone).
//
// Floats follow the x87 (game/mechanics/fpu.h): 24-bit precision in game.

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "audio/environment_presets.h"
#include "game/mechanics/fpu.h"

namespace hailstone::audio {

// Listener environments (EqSoundManager +0xff8; emitters with an environment only play in theirs).
enum ListenerEnvironment : std::int32_t { kEnvironmentOutside = 1, kEnvironmentHigh = 2, kEnvironmentLow = 3 };

// The special room types 0x48a480 sets (EAX environments 22 and 23).
constexpr std::int32_t kRoomUnderwater = 0x16, kRoomDrugged = 0x17;

// CDisplay_UpdateListenerEnvironment: nothing without a local player.
std::optional<std::int32_t> listener_environment(bool have_player, float ceiling, float floor, bool indoors,
                                                 game::mechanics::FpuPrecision precision =
                                                     game::mechanics::FpuPrecision::Single24);

// The indoor test of CDisplay::SetupEmitterEnvironment: the player actor's scaled ambient below 0.48 (0x9c78bc).
constexpr float kIndoorAmbient = 0.48f;

// The room types (Miles AIL_set_3D_room_type) a zone uses for each environment.
struct RoomPresets {
    std::int32_t outside = 19; // +0xfe8 (EAX "plain")
    std::int32_t high = 2;     // +0xfe0 ("room")
    std::int32_t low = 12;     // +0xfe4 ("hallway")
    std::int32_t for_environment(std::int32_t environment) const {
        return environment == kEnvironmentLow ? low : environment == kEnvironmentHigh ? high : outside;
    }
};
// SetCurrentZone's table (case-insensitive name match, the first that matches), else the defaults. The table is
// read from the player's eqgame.exe (audio/environment_presets.h); empty, every zone has the defaults.
RoomPresets zone_room_presets(std::span<const ZoneRoomPreset> table, std::string_view zone_short_name);

// Miles' parameters for a room type (mss32.dll's table at 0x2114d070: 26 rows of {level, decay time (s), damping,
// predelay (s)}, the EAX 1 environments; room 0 has level 0, no reverb). Out of range: room 0.
struct RoomReverb {
    float level = 0.0f, decay_s = 1.493f, damping = 0.5f, predelay_s = 0.011f;
};
RoomReverb miles_room_reverb(std::int32_t room);

// What EqSoundManager::SetListenerLocation passes to SoundManager::SetListenerLocation (float bits): y, x, -z
// moved through the x87 (a signalling NaN comes out quiet) and heading * 2pi (double) * 1/512 (double) rounded once
// to a float.
struct ListenerLocation {
    std::uint32_t x = 0, y = 0, z = 0, angle = 0;
};
ListenerLocation eq_listener_location(std::uint32_t y, std::uint32_t x, std::uint32_t z, std::uint32_t heading,
                                      game::mechanics::FpuPrecision precision = game::mechanics::FpuPrecision::Single24);

// SoundManager::SetListenerLocation: what it stores (+0..+0xc) and, with a 3D listener, the Miles position and
// orientation: face (cos a, sin a, 0), up (0, 0, -1), the sine and cosine from fsin / fcos (as the x87 computes
// them: its 66-bit pi for the reduction, and an operand of 2^63 or more left as it is).
struct ListenerOrientation {
    std::uint32_t stored[4]{};   // x, y, z, angle
    std::uint32_t position[3]{}; // AIL_set_3D_position
    std::uint32_t face[3]{};     // AIL_set_3D_orientation
    std::uint32_t up[3]{};
};
ListenerOrientation sound_manager_listener(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t angle);

// fsin / fcos of a float operand, rounded to a float (bits).
std::uint32_t x87_fsin(std::uint32_t operand);
std::uint32_t x87_fcos(std::uint32_t operand);

// PlayerZoneClient::LegalPlayerRace @0x49deb0: `race` -1 means the spawn's own race; the playable races are every
// race up to 12 (signed, so 0 and negatives count) and 128, 130, 330, 522.
bool legal_player_race(std::int32_t race, std::int32_t spawn_race);

} // namespace hailstone::audio
