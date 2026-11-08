#pragma once

// CharacterZoneClient::FindAffectSlot @0x43d300 and the stacking rules it runs (rof2decomp batch 260:
// re/src/game/character/find_affect_slot.cpp, re/src/game/spell/stacking_predicates.cpp,
// re/src/game/character/{last_effect_slot,spell_level_checks,zone_client_checks,stat_calc}.cpp): where a buff that
// lands goes on the character, or nowhere when an existing buff blocks it. EQ_Character::HitBySpell is its only
// caller (game/mechanics/hit_by_spell.h, the buff path).
//
// In the original's order:
//  1. no caster, an invalid spell id or no record: none;
//  2. a caster level of 0 or less is the caster's level, plus (for a bard song, BardCastBard) the caster's cached
//     SPA 0x110 totals (buffs, AAs, items for a player);
//  3. a player caster: IsStackBlocked (an existing buff's SPA 148 blocks one of the spell's effects);
//  4. a buff of the same spell is refreshed in place, unless it came from another caster and the spell is a
//     stackable DoT (detrimental, a duration, SPA 0 or 424, +0x239 clear): then a slot holding the same spell from
//     a missing caster or the character itself, else the first empty slot; a lower caster level than the buff's is
//     refused (except SPA 444), and SPA 67 / 101 / 113 refuse a refresh;
//  5. every existing buff is compared effect by effect: SPA 254 ends the comparison, a new SPA 45 refuses, SPAs
//     IsSPAIgnoredByStacking skips, SPA 446..449 refuse a smaller base (IsSPAStacking), the per-SPA rules (1, 10,
//     161/162, 332, 416, 444, 450..452), then the detrimental, click-off, SPA 101 and special-spell refusals, and
//     CalcAffectChange of the old buff against the new spell decides whether it overwrites the slot;
//  6. else a free slot (GetOpenEffectSlot: songs from max - 21, the last slot for a short buff of the right kind),
//     else for a beneficial spell on a character whose spawn allows it, the first buff whose spell may be dropped.
// The slot's EQ_Affect is returned (GetEffect: slot 0 for an index outside 0..96, as the original does); *slot is -1
// when none. Buffs whose spell has no record are cleared on the way (type 0, spell -1, modifier 1.0).
//
// Checked against CharacterZoneClient_FindAffectSlot.jsonl (1600, with the real stacking rules) and
// EQ_Spell_StackingSpaTables.jsonl (tests/spell_gems_vectors_test.cpp).

#include <cstdint>

#include "game/mechanics/hit_by_spell.h"

namespace hailstone::game::mechanics::affect {

using hit::Affect;
using hit::Character;
using hit::Spawn;
using hit::Spell;

inline constexpr int kAffectSlots = 97; // GetEffect: 0..41 long buffs, 42..96 short buffs

// The callees FindAffectSlot and its helpers reach through function or virtual pointers.
class AffectQueries {
public:
    virtual ~AffectQueries() = default;
    virtual const Spell* spell_by_id(std::int32_t id) = 0;  // pinstSpellManager vtable +0xc
    virtual Affect* effect_at(std::int32_t slot) = 0;        // the profile's buff 0..96
    virtual std::uint32_t buff_caster_id(std::int32_t slot) = 0; // profile +0x2218 + 4 x slot
    virtual std::uint8_t get_level(Spawn* spawn) = 0;        // PlayerZoneClient::GetLevel
    virtual void recache_spell_effects(Character* c) = 0;    // CharacterZoneClient_ReCacheSpellEffects
    virtual std::int32_t cached_buff_effect(Character* c, std::int32_t spa) = 0; // (spa, 0)
    virtual std::int32_t cached_aa_effect(Character* c, std::int32_t spa) = 0;
    virtual std::int32_t cached_item_effect(Character* c, std::int32_t spa) = 0;
    virtual std::uint32_t extra_buff_slots() = 0;            // the character's vtable +0x2c
    virtual std::uint8_t spawn_vf_dc(Spawn* spawn) = 0;      // the spawn's vtable +0xdc: no buff may be dropped
    virtual Spawn* spawn_by_id(std::uint32_t id) = 0;        // PlayerManagerClient::GetSpawnByID
    virtual void remove_affect(Affect* a) = 0;               // CharacterZoneClient_RemoveAffect(a, 0, 0)
    // CharacterZoneClient::CalcAffectChange(spell, level, slot, 0, buff_index, 0, 0, -1, 1).
    virtual std::int32_t calc_affect_change(const Spell* spell, std::uint32_t level, std::uint32_t slot,
                                            std::int32_t buff_index) = 0;
};

// CharacterZoneClient::FindAffectSlot(spell_id, caster, &slot, keep, caster_level, 0, 0, unused). `keep` (low byte)
// keeps the buff being replaced (no RemoveAffect). `self` is the character the buff lands on (its spawn may be null).
Affect* find_affect_slot(Character* self, std::int32_t spell_id, Spawn* caster, std::int32_t* slot, std::uint32_t keep,
                         std::uint32_t caster_level, AffectQueries& q);

// The helpers, exposed for the client and the tests.
bool is_spa_stacking(std::int32_t spa);           // EQ_Spell::IsSPAStacking @0x4ace40: SPA 446..449
bool is_spa_ignored_by_stacking(std::int32_t spa); // EQ_Spell::IsSPAIgnoredByStacking @0x4aca70
bool is_stackable_dot(const Spell& spell);        // EQ_Spell_IsStackableDot @0x4324b0
bool is_click_off_allowed(const Spell& spell);    // EQ_Spell::IsClickOffAllowed @0x41d890
bool is_res_effect_spell(std::int32_t id);        // __IsResEffectSpell @0x432590: 756, 5249, 13087
std::uint8_t spell_level_needed(const Spell& spell, std::int32_t class_id); // EQ_Spell::GetSpellLevelNeeded
std::uint32_t bard_cast_bard(const Spell* spell, std::uint32_t class_id); // CharacterZoneClient::BardCastBard
std::uint8_t max_effects(const Character* self);  // CharacterZoneClient::GetMaxEffects @0x432630
// The player's extra long-buff slots (PcZoneClient's CharacterZoneClient vtable +0x2c, @0x580460): one from level 72,
// two from 75 (the CharacterBase's virtual +0x68, the current level, compared twice).
std::uint32_t pc_extra_buff_slots(std::int32_t level);
Affect* get_effect(std::int32_t slot, AffectQueries& q); // CharacterZoneClient_GetEffect @0x433df0
// CharacterZoneClient::GetLastEffectSlot @0x438ee0 / GetOpenEffectSlot @0x43a6d0 (low bytes of the flags).
std::int32_t last_effect_slot(Character* self, std::uint32_t first, std::uint32_t second, std::uint32_t third,
                              AffectQueries& q);
std::int32_t open_effect_slot(Character* self, std::uint32_t short_only, std::uint32_t song, std::int32_t fallback,
                              AffectQueries& q);
// CharacterZoneClient::IsStackBlocked @0x4341e0 on the character's own effects.
bool is_stack_blocked(Character* self, const Spell* spell, std::uint8_t caster_class, AffectQueries& q);

} // namespace hailstone::game::mechanics::affect
