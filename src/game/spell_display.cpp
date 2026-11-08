#include "game/spell_display.h"

#include <cstdio>
#include <vector>

#include "game/eq_strings.h"
#include "game/mechanics/character.h"
#include "game/mechanics/tables.h"
#include "game/spell_description.h"
#include "game/spells.h"

namespace hailstone::game {

std::string format_duration_hms(std::int32_t seconds) {
    std::int32_t h = seconds / 3600, m = (seconds % 3600) / 60, s = seconds % 60;
    char b[32];
    if (h < 1) {
        if (m < 1) std::snprintf(b, sizeof b, "%ds", s);
        else if (s > 0) std::snprintf(b, sizeof b, "%dm %ds", m, s);
        else std::snprintf(b, sizeof b, "%dm", m);
    } else if (m > 0 && s > 0) {
        std::snprintf(b, sizeof b, "%dh %dm %ds", h, m, s);
    } else if (m > 0) {
        std::snprintf(b, sizeof b, "%dh %dm", h, m);
    } else if (s > 0) {
        std::snprintf(b, sizeof b, "%dh %ds", h, s);
    } else {
        std::snprintf(b, sizeof b, "%dh", h);
    }
    return b;
}

SpellDisplay build_spell_display(const SpellData& spell, const SpellDisplayContext& ctx) {
    auto str = [&](std::uint32_t id) { return ctx.strings ? ctx.strings->format(id, {}) : std::string(); };
    auto fmt = [&](std::uint32_t id, std::vector<std::string> args) {
        return ctx.strings ? ctx.strings->format(id, args) : std::string();
    };
    SpellDisplay d;
    d.title = str(0xc1d) + "  " + spell.name; // "Spell:" + "  " + the name
    std::string& t = d.description;
    // CSpellDisplayWnd_FormatSpellDuration: SpellDuration at the player's level, in 6-second ticks.
    std::int32_t seconds = ctx.character ? ctx.character->spell_duration(spell, static_cast<std::uint8_t>(ctx.player_level)) * 6 : 0;
    if (spell.duration_formula != 0) {
        if (seconds >= 1) t += fmt(0xc1e, {format_duration_hms(seconds)}) + "<BR>";
        else t += str(0xc1f) + "<BR>";
    }
    // EQ_Spell::GetSpellLevelNeeded for the player's class: Level, Skill, Mana.
    std::uint8_t level = ctx.player_class >= 1 && ctx.player_class <= 16 ? spell.class_level[ctx.player_class - 1u] : 255;
    if (level < 0xfe) {
        t += fmt(0x1fa, {std::to_string(level)}) + "<BR>";
        t += fmt(0xc21, {str(mechanics::skill_name_eqstr(spell.skill))}) + "<BR>";
        if (spell.mana > 0) t += fmt(0xbbe, {std::to_string(spell.mana)}) + "<BR>";
    }
    if (ctx.player_class != 8) { // not for bards: "Cast: <s>" (and the adjusted time when anything adjusts it)
        // The cast time CastSpell would use: the hybrids' quick cast and the SPA 127 foci, from the player's
        // character (none known yet: the spell's own). SetSpell @0x75f3c6..0x75f428 adds "(x sec)" when either
        // GetCastingTimeModifier or GetFocusCastingTimeModifier (4th argument 0) is non-zero, even when they cancel
        // out, not when the time differs.
        std::uint32_t ms = ctx.character ? mechanics::cast_time_ms(*ctx.character, spell) : spell.cast_time;
        const bool modified = ctx.character && (ctx.character->casting_time_modifier(spell) != 0 ||
                                                ctx.character->focus_casting_time_modifier(spell, false) != 0);
        char base[32], adj[32];
        std::snprintf(base, sizeof base, "%1.1f", static_cast<double>(spell.cast_time) * 0.001);
        if (!modified) {
            t += fmt(0xc23, {base}) + "<BR>";
        } else {
            std::snprintf(adj, sizeof adj, "(%1.1f sec)", static_cast<double>(static_cast<float>(ms) * 0.001f));
            t += fmt(0xc22, {base, adj}) + "<BR>";
        }
    }
    if (spell.reuse_timer_index > 0) t += fmt(0x317, {std::to_string(spell.reuse_timer_index)}) + "<BR>";
    if (ctx.show_spell_descriptions && spell.description_id != 0 && ctx.db_string) {
        std::string text = spell_description_text(spell, ctx.db_string(static_cast<std::uint32_t>(spell.description_id), 6),
                                                  ctx.character, ctx.player_level, ctx.player_class);
        if (text.rfind("Unknown", 0) != 0) t += fmt(0x213b, {text}) + "<BR>";
    }
    d.icon = spell.spell_icon;
    return d;
}

} // namespace hailstone::game
