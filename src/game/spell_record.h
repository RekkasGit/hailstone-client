#pragma once

// The original's spell record (EQ_Spell, 0x4ed bytes in this build) as
// EQ_Spell_InitFromLine @0x4ae710 fills it from a spells_us.txt line, and the
// checksums made from it: EQ_Spell_CalcCRC @0x4aca30 (the per-spell CRC that
// OP_CastSpell carries, SpellManager_GetSpellCRC @0x432560) and
// ComputeFileChecksum @0x808d90 (the file CRCs of OP_ZoneEntry +68/+72).
//
// Follows the original's field-by-field parse and CRC, checked against the original's EQ_Spell_CalcCRC vectors (642
// real spells_us.txt lines, the record bytes and the CRC).

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace hailstone::game {

inline constexpr std::size_t kSpellRecordSize = 0x4ed;
inline constexpr std::size_t kSpellCrcBytes = 0x4e8; // EQ_Spell_CalcCRC checksums +0..+0x4e7
using SpellRecord = std::array<std::uint8_t, kSpellRecordSize>;

// EQ_Spell_InitFromLine(line): the record cleared to zero, then its 237
// caret-separated fields (atoi / atof / text fields cut to their size; a line
// end or the end of the text leaves the remaining fields empty).
SpellRecord spell_record_from_line(std::string_view line);
// EQ_Spell_InitFromLine(nullptr): the "Unknown Spell" every id of the table
// starts as (LoadTextSpells fills all 45001 slots with its CRC).
SpellRecord unknown_spell_record();

// __MemChecker0 @0x5643f0: CRC-32 (reflected 0xedb88320) from 0xffffffff
// with no final inversion; nothing for size <= 0.
std::uint32_t mem_checker0(std::span<const std::uint8_t> data);
// __MemChecker1 @0x808cb0 with seed 0: the standard CRC-32 (zlib's crc32).
std::uint32_t mem_checker1(std::span<const std::uint8_t> data);

// EQ_Spell_CalcCRC: __MemChecker0 of bytes +0..+0x4e7, its bytes (b0 b1 b2 b3)
// reordered to b1 | b3 << 8 | b0 << 16 | b2 << 24.
std::uint32_t spell_record_crc(const SpellRecord& record);

} // namespace hailstone::game
