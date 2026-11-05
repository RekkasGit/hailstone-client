#pragma once

// Human-readable names for race and class ids, for display only.
// Ids follow the RoF2 client and EQEmu (common/races.h, common/classes.h).

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace nmsc::game {

// CEverQuest::GetLangDesc @0x5159f0: the eqstr id of a language's name (EQEmu's language ids;
// 3115 for unknown ones).
std::uint32_t language_eqstr(std::uint32_t lang);

// Race name, or "Race <id>" for ids not in the table.
std::string race_name(std::uint32_t race_id);
// True for the 16 races a player can create (1-12, 128, 130, 330, 522).
bool is_playable_race(std::uint32_t race_id);

// Class name ("Shadow Knight"), or "Class <id>" for unknown ids.
std::string class_name(std::uint32_t class_id);
// Three-letter abbreviation ("SHD"), or the full name for non-player classes.
std::string class_short_name(std::uint32_t class_id);
// Joins several classes for multiclass display: "Warrior / Cleric / Wizard".
// Zero ids are skipped. With abbreviate, uses the short names: "WAR/CLR/WIZ".
std::string class_list_name(std::span<const std::uint32_t> class_ids, bool abbreviate = false);
// A class bit mask (bit c-1 = class c, classes 1..16) as the multiclass display shows it: "War/Clr/Wiz"; "" for
// none.
std::string class_mask_name(std::uint32_t mask);

} // namespace nmsc::game
