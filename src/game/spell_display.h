#pragma once

// The spell display window's text (CSpellDisplayWnd::SetSpell @0x75f060, the spell book / gem branch): the title
// "Spell:  <name>", then the duration, level, skill, mana, cast time and timer lines and the spell's description
// ("Spell Effect:  ..."), STML lines ended by <BR>.

#include <cstdint>
#include <functional>
#include <string>

namespace hailstone::game {

class EqStrings;
struct SpellData;
namespace mechanics { class Character; }

struct SpellDisplayContext {
    const EqStrings* strings = nullptr;
    const mechanics::Character* character = nullptr;
    std::int32_t player_level = 1;
    std::uint8_t player_class = 0;
    std::function<std::string(std::uint32_t id, std::uint32_t type)> db_string;
    bool show_spell_descriptions = true; // eqclient.ini Defaults/ShowSpellDescriptions
};

struct SpellDisplay {
    std::string title;
    std::string description;
    std::int32_t icon = -1; // A_SpellIcons cell
};

SpellDisplay build_spell_display(const SpellData& spell, const SpellDisplayContext& ctx);

// FormatDurationHMS @0x897a40: "%dh %dm %ds", "%dh %dm", "%dh %ds", "%dh", "%dm %ds", "%dm" or "%ds".
std::string format_duration_hms(std::int32_t seconds);

} // namespace hailstone::game
