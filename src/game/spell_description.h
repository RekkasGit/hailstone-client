#pragma once

// A spell's description text (dbstr_us.txt kind 6) with its codes filled in,
// as GetSpellDescriptionText @0x4fc170 does it for the spell display and the
// item display windows.
//
// Follows the original's formatter (0x7f9550) and its four code handlers, checked
// against the original's 1000 GetSpellDescriptionText vectors
// (tests/game_vectors_test.cpp). The codes, each ended by whitespace (9..13),
// one of !.-:;, '" or the end (the ender is kept as text):
//   #n  |Base| of effect slot n; for an HP effect (SPA 0, 79, 334) or SPA 100,
//       |CalcAffectChange| at the player's level when the player's class has
//       the spell by then, else at the lowest level any class 1..16 gets it
//       (at most 100); SPA 0 with formula 123 keeps |Base|.
//   $n  |Base2|; for SPA 0, 79 or 334 |CalcAffectChange| at that lowest level.
//   @n  |Max|; for SPA 0, 79 or 334 |CalcAffectChange| at level 100.
//   %y / %z  " h:mm:ss" of the player's duration of the spell (SpellDuration
//       at the player's level) / of the spell's duration cap, in 6 s ticks;
//       another letter gives " 0:00:00".
// n is atoi of the token; n < 1 gives "#ERR" (as does n > 12, where the
// original stops on an array bounds check). "|x|" keeps INT_MIN negative.

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace hailstone::game {

struct SpellData;
namespace mechanics { class Character; }

// What the handlers read of the spell and the player, and the callees they
// ask, in the original's call order.
struct SpellDescriptionContext {
    struct Slot {
        std::int32_t spa = 254;   // EQ_Spell +0x11c (Attrib)
        std::int32_t base = 0;    // +0x2c
        std::int32_t base2 = 0;   // +0x5c
        std::int32_t max = 0;     // +0x8c
        std::int32_t calc = 0;    // +0xec (formula)
    };
    std::array<Slot, 12> slots{};
    std::int32_t duration_cap = 0;  // +0x20 (%z)
    bool has_player = true;         // pinstLocalPC (the original's %y needs one)
    std::int32_t player_level = 1;  // the PC's virtual +0x68
    std::uint8_t player_class = 0;  // profile +0x3374
    // EQ_Spell::GetSpellLevelNeeded(class).
    std::function<std::uint8_t(std::uint32_t cls)> level_needed;
    // CharacterZoneClient::CalcAffectChange(spell, level, slot, 0, -1, 0, 0, -1, 1); slot 0-based.
    std::function<std::int32_t(std::int32_t level, std::uint32_t slot)> calc_affect_change;
    // CharacterZoneClient::SpellDuration(spell, level, 0) in ticks.
    std::function<std::int32_t(std::int32_t level)> spell_duration;
};

// The formatter over `text` (the dbstr template), into the original's
// 0x400-byte output (at most 0x3ff characters).
std::string format_spell_description(std::string_view text, const SpellDescriptionContext& ctx);

// The context for a client spell: the slots from the spell record, the level
// and duration callees from `character` (CalcAffectChangeGeneric with no buff,
// SpellDuration), EQ_Spell::GetSpellLevelNeeded from the record. Without a
// character, CalcAffectChange gives |Base| and SpellDuration 0.
SpellDescriptionContext spell_description_context(const SpellData& spell, const mechanics::Character* character,
                                                  std::int32_t player_level, std::uint8_t player_class);

// GetSpellDescriptionText for a client spell: `text` is dbstr kind 6 of the
// spell's description id (or another id the caller chose).
std::string spell_description_text(const SpellData& spell, std::string_view text,
                                   const mechanics::Character* character, std::int32_t player_level,
                                   std::uint8_t player_class);

} // namespace hailstone::game
