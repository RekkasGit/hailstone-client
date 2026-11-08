#include "game/spell_description.h"

#include <cstdio>
#include <cstring>
#include <optional>

#include "core/msvc_atoi.h"
#include "game/mechanics/character.h"
#include "game/spells.h"

namespace hailstone::game {

namespace {

constexpr std::int32_t kNoLevel = 100;
constexpr const char* kDelimiters = "!.-:;, '\""; // 0xa1021c
constexpr const char* kError = "#ERR";            // 0xa10228

using Ctx = SpellDescriptionContext;

// "%d" of |value| (cdq / xor / sub: INT_MIN stays negative).
std::string number(std::int32_t value) {
    auto sign = static_cast<std::uint32_t>(value >> 31);
    auto magnitude = static_cast<std::int32_t>((static_cast<std::uint32_t>(value) ^ sign) - sign);
    return std::to_string(magnitude);
}

// The slot of a "#n" / "$n" / "@n" token: n - 1; none for n - 1 < 0 or (the
// original's bounds check) 12 and up.
std::optional<std::uint32_t> slot_of(std::string_view token) {
    auto slot = static_cast<std::uint32_t>(core::msvc_atoi(token)) - 1u;
    if (static_cast<std::int32_t>(slot) < 0 || slot >= 12) return std::nullopt;
    return slot;
}

bool hp_spa(std::int32_t spa) { return spa == 0 || spa == 0x4f || spa == 0x14e; }

std::int32_t lowest_level(const Ctx& c) {
    std::int32_t level = kNoLevel;
    for (std::uint32_t cls = 1; cls <= 16; ++cls) {
        auto needed = static_cast<std::int32_t>(c.level_needed(cls));
        if (needed < level) level = needed;
    }
    return level;
}

// The value of a slot: `raw`, or for an HP effect CalcAffectChange at `level`,
// SPA 0 with formula 123 keeping the raw entry.
std::int32_t slot_value(const Ctx& c, std::uint32_t slot, std::int32_t raw, bool hp, std::int32_t level) {
    if (!hp) return raw;
    std::int32_t value = c.calc_affect_change(level, slot);
    if (c.slots[slot].spa == 0 && c.slots[slot].calc == 0x7b) value = raw;
    return value;
}

std::optional<std::string> handle_base(std::string_view token, const Ctx& c) { // '#' 0x4fbef0
    auto slot = slot_of(token);
    if (!slot) return std::nullopt;
    const Ctx::Slot& s = c.slots[*slot];
    std::int32_t value = s.base;
    if (hp_spa(s.spa) || s.spa == 0x64) {
        std::int32_t level = kNoLevel;
        bool own = false;
        if (c.has_player) {
            auto needed = static_cast<std::int32_t>(c.level_needed(c.player_class));
            if (c.player_level >= needed) {
                level = c.player_level;
                own = true;
            }
        }
        if (!own) level = lowest_level(c);
        value = slot_value(c, *slot, s.base, true, level);
    }
    return number(value);
}

std::optional<std::string> handle_base2(std::string_view token, const Ctx& c) { // '$' 0x4fc070
    auto slot = slot_of(token);
    if (!slot) return std::nullopt;
    const Ctx::Slot& s = c.slots[*slot];
    bool hp = hp_spa(s.spa);
    return number(slot_value(c, *slot, s.base2, hp, hp ? lowest_level(c) : 0));
}

std::optional<std::string> handle_max(std::string_view token, const Ctx& c) { // '@' 0x4fbe10
    auto slot = slot_of(token);
    if (!slot) return std::nullopt;
    const Ctx::Slot& s = c.slots[*slot];
    return number(slot_value(c, *slot, s.max, hp_spa(s.spa), kNoLevel));
}

std::optional<std::string> handle_duration(std::string_view token, const Ctx& c) { // '%' 0x4e1bb0
    std::uint32_t ticks = 0;
    char first = token.empty() ? '\0' : token[0];
    char up = first >= 'a' && first <= 'z' ? static_cast<char>(first - 32) : first; // _toupper, C locale
    if (up == 'Y') ticks = static_cast<std::uint32_t>(c.spell_duration(c.player_level));
    else if (up == 'Z') ticks = static_cast<std::uint32_t>(c.duration_cap);
    std::uint32_t seconds = ticks * 6u;
    char text[48];
    std::snprintf(text, sizeof text, " %d:%02d:%02d", static_cast<int>(seconds / 3600),
                  static_cast<int>(seconds / 60 % 60), static_cast<int>(seconds % 60));
    return std::string(text);
}

} // namespace

std::string format_spell_description(std::string_view text, const SpellDescriptionContext& ctx) {
    constexpr std::int32_t kSize = 0x400;
    using Handler = std::optional<std::string> (*)(std::string_view, const Ctx&);
    auto handler_for = [](char c) -> Handler {
        switch (c) {
        case '#': return handle_base;
        case '$': return handle_base2;
        case '@': return handle_max;
        case '%': return handle_duration;
        default: return nullptr;
        }
    };
    std::string out;
    std::string token;
    Handler handler = nullptr;
    bool in_token = false;
    std::size_t i = 0;
    auto at = [&](std::size_t k) { return k < text.size() ? text[k] : '\0'; };
    for (;;) {
        char c = at(i);
        if (!in_token) {
            if (Handler h = handler_for(c)) {
                handler = h;
                in_token = true;
                token.clear();
            } else if (c == 0 || static_cast<std::int32_t>(out.size()) >= kSize - 1) {
                break;
            } else {
                out += c;
            }
        } else {
            auto space = static_cast<std::uint8_t>(c - 9);
            bool delimiter = c == 0 || std::strchr(kDelimiters, c) != nullptr; // strchr finds the terminator too
            if (space > 4 && !delimiter) {
                token += c;
            } else {
                std::optional<std::string> r = handler(token, ctx);
                std::string replacement = r ? *r : std::string(kError);
                std::int32_t room = kSize - 1 - static_cast<std::int32_t>(out.size());
                if (!replacement.empty()) {
                    // Copied while the room lasts; when it runs out the output ends before the replacement.
                    if (room > 0 && static_cast<std::int32_t>(replacement.size()) >= room) break;
                    out += replacement;
                }
                if (space <= 4 || delimiter) --i; // the ender is read again as text
                in_token = false;
            }
        }
        ++i;
    }
    return out;
}

SpellDescriptionContext spell_description_context(const SpellData& spell, const mechanics::Character* character,
                                                  std::int32_t player_level, std::uint8_t player_class) {
    SpellDescriptionContext c;
    for (std::size_t k = 0; k < 12; ++k) {
        c.slots[k].spa = spell.effect[k];
        c.slots[k].base = spell.base[k];
        c.slots[k].base2 = spell.base2[k];
        c.slots[k].max = spell.max[k];
        c.slots[k].calc = spell.formula[k];
    }
    c.duration_cap = static_cast<std::int32_t>(spell.duration_cap);
    c.player_level = player_level;
    c.player_class = player_class;
    // EQ_Spell::GetSpellLevelNeeded @0x4af700: classes 1..35 read ClassLevel[cls] (+0x246 + cls; 17..35 are 0 in
    // a parsed record), any other class the warrior entry.
    c.level_needed = [&spell](std::uint32_t cls) -> std::uint8_t {
        if (cls - 1u > 34u) return spell.class_level[0];
        return cls <= 16 ? spell.class_level[cls - 1] : 0;
    };
    c.calc_affect_change = [&spell, character](std::int32_t level, std::uint32_t slot) -> std::int32_t {
        if (!character) {
            std::int32_t b = spell.base[slot];
            return b < 0 ? static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(b)) : b;
        }
        return character->calc_affect_change(spell, static_cast<std::uint32_t>(level), static_cast<int>(slot), nullptr,
                                             spell.base[slot], true);
    };
    c.spell_duration = [&spell, character](std::int32_t level) -> std::int32_t {
        return character ? character->spell_duration(spell, static_cast<std::uint8_t>(level)) : 0;
    };
    return c;
}

std::string spell_description_text(const SpellData& spell, std::string_view text,
                                   const mechanics::Character* character, std::int32_t player_level,
                                   std::uint8_t player_class) {
    return format_spell_description(text, spell_description_context(spell, character, player_level, player_class));
}

} // namespace hailstone::game
