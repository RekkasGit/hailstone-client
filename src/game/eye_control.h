#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace hailstone::game {
struct WorldState;

// ProcessAffects SPA67 uses a 25-byte terminated copy of the player's name, then searches for its Eye summon.
std::string eye_name_for(std::string_view player);
std::uint32_t find_eye_summon(const WorldState& world);
// True while `eye` is a live, non-corpse spawn named as this player's Eye. Once control is held this is what keeps
// it: another same-prefix Eye appearing later must not make the held one look ambiguous.
bool is_own_eye(const WorldState& world, std::uint32_t eye);
bool take_eye_control(WorldState& world, std::uint32_t eye);
bool release_eye_control(WorldState& world);
} // namespace hailstone::game
