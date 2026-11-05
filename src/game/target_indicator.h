#pragma once

// The 3D target indicator (TargetIndicator, pinstTargetIndicator): the ring at the target's feet, coloured by how
// the target cons. Its settings come from the UI skin's TargetIndicator.ini (the default UI's when the skin has
// none; TargetIndicator_LoadIni @0x5c4d60): one section per con colour, Trivial, VeryEasy, Easy, FairlyEasy,
// FairMatch, Difficult, Deadly, then Free and FreeInvalid (the free-target marker), each read by
// TargetIndicator_LoadIndicatorSettings @0x5c4870. The Options window's Show3dTargetIndicator
// (TargetIndicator_SetEnabled @0x5c3f10, from DisplayPageWndNotification) and /indicator on|off turn it on and off.
//
// The original builds four concentric UV-animated circles (FUN_005c46f0, FUN_005c4430); this client draws the
// ring its own way: only which ring shows, its colour and alpha and its size follow the original.

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "game/combat.h"

namespace nmsc {
class IniFile;
}

namespace nmsc::game {

// One section's colour (LoadIndicatorSettings: Alpha default 0xc0, Red / Green / Blue default 0xff, each kept as a
// byte) and FloorOffset (default 1.0).
struct TargetRingStyle {
    std::uint8_t alpha = 0xc0, red = 0xff, green = 0xff, blue = 0xff;
    float floor_offset = 1.0f;
};

inline constexpr std::array<std::string_view, 9> kTargetIndicatorSections{
    "Trivial", "VeryEasy", "Easy", "FairlyEasy", "FairMatch", "Difficult", "Deadly", "Free", "FreeInvalid"};

struct TargetIndicatorSettings {
    std::array<TargetRingStyle, 9> styles{}; // by kTargetIndicatorSections (TargetIndicator +0x1c, 0xac apart)
    // A missing section or key keeps LoadIndicatorSettings' default.
    static TargetIndicatorSettings parse(const IniFile& ini);
};

// TargetIndicator_UpdateTargetRing @0x5c5250: the section the target's PcClient::GetConLevel picks (gray 0
// Trivial .. red 6 Deadly); none for con 0 (corpses and the rest GetConLevel gives 0), which hides the ring.
// With no target, or the indicator off, the ring is hidden too.
std::optional<int> target_ring_section(Con con);

// FUN_005c46f0 @0x5c46f0: the ring's size, the target's AvatarHeight (+0x138; 3 without a target) times its
// actor's scale when it has an actor, at least 2.
float target_ring_radius(float avatar_height, float actor_scale, bool has_actor);

} // namespace nmsc::game
