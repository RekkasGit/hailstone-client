#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace nmsc::game {

// Names from the original client's zone table; empty for unknown ids.
std::string zone_short_name(std::uint32_t zone_id);
std::string zone_long_name(std::uint32_t zone_id);
// A zone's id by its short name (any case), 0 when unknown.
std::uint32_t zone_id_by_short_name(std::string_view short_name);
// The zone's flags (EQZoneInfo +0x198, the zone table's), 0 for unknown ids.
std::uint32_t zone_flags(std::uint32_t zone_id);
// EQWorldData::ExpansionZone @0x7dc510: the zone's expansion (EQZoneInfo +8; 6 for the Lost Dungeons of Norrath
// adventures), 0 for unknown ids (ids above 1000 masked with 0x7fff).
std::uint32_t zone_expansion(std::uint32_t zone_id);
// EQWorldData::IsNewbieZone @0x7dc550: flag 0x2 (ids above 1000 masked with 0x7fff).
bool is_newbie_zone(std::uint32_t zone_id);

} // namespace nmsc::game
