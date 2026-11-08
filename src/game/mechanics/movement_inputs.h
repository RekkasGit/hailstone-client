#pragma once

// The character values one movement frame reads (app/player_movement.cpp), taken
// from a Character (the exact ports of CharacterZoneClient: TotalEffect,
// ProcessAllStats, GetAdjustedSkill, CalcCarriedWeight, the heroic getters)
// instead of from the raw profile and the buffs' base values.
//
// The original's movement code reads these from the live character each frame:
//   - the speed rate: TotalEffect(99) and TotalEffect(3), the SPA 271 floor
//     while running and the SPA 290 speed cap raise (ProcessControls, Step);
//   - the swim speed: GetAdjustedSkill(50);
//   - the weight factor: EQ_Character::CalculateWeight @0x44cdd0, which reads
//     CurrWeight (CalcCarriedWeight @0x44d020: the items' own weights,
//     charm-scaled, bag reductions, TotalEffect(221)), STR with buffs
//     (CharacterBase +0x4a0, the capacity), Cur_HP and Max_HP, and
//     CharacterBase +0x460;
//   - ApplyFallDamage @0x45ac60: CurrWeight, GetAdjustedSkill(39), the profile's
//     Safe Fall skill, TotalEffect(228) and the heroic AGI bonus;
//   - MaxAirSupply @0x581da0 / ProcessAirSupply: STA with buffs, TotalEffect(246)
//     and (14); the lava and cold damage: the fire / cold / disease resists;
//   - UpdateLevitationState @0x433480: the buff cache's SPA 57 values.
// The Character must have had process_all_stats() run (StatsUi does it each
// frame): STR, STA, the resists and the heroic bonuses are ProcessAllStats'
// results. See docs/mechanics.md "Movement inputs".

#include <cstdint>

namespace hailstone::game::mechanics {

class Character;

struct MovementFacts {
    std::int32_t spa99 = 0, spa3 = 0, spa271 = 0, spa290 = 0; // the movement rate, floor and cap raise
    std::int32_t spa228 = 0, spa246 = 0, spa14 = 0;           // safe fall, air supply, water breathing
    std::int32_t swimming_skill = 0;                          // GetAdjustedSkill(50)
    std::int32_t safe_fall_adjusted = 0;                      // GetAdjustedSkill(39)
    std::int32_t safe_fall_base = 0;                          // the profile's Safe Fall (skills +0x313c)
    std::int32_t heroic_agi = 0;                              // CharacterZoneClient_GetHeroicAGIBonus
    std::int32_t carried = 0;                                 // CurrWeight, CalcCarriedWeight
    std::int32_t strength = 0, stamina = 0;                   // STR / STA with items and buffs
    std::int32_t hp = 0, max_hp = 0;                          // Cur_HP / Max_HP (CalculateWeight's)
    bool low_level_exempt_off = false;                        // CharacterBase +0x460
    std::int32_t levitate_mode2 = 0, levitate_mode5 = 0;      // SpellCache_GetBuffValue(57, 0 / 1)
    std::int32_t sv_fire = 0, sv_cold = 0, sv_disease = 0;    // the resists the region damage reads
};

// Everything above from a built character.
MovementFacts movement_facts(const Character& ch);

} // namespace hailstone::game::mechanics
