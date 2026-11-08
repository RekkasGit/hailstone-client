#include "game/mechanics/movement_inputs.h"

#include "game/mechanics/character.h"

namespace hailstone::game::mechanics {

namespace {
constexpr std::uint32_t kSwimming = 50; // EQ skill ids
constexpr std::uint32_t kSafeFall = 39;
} // namespace

MovementFacts movement_facts(const Character& ch) {
    MovementFacts f;
    // TotalEffect @0x438990 with items, AAs and buffs (the defaults).
    f.spa99 = ch.total_effect(99);
    f.spa3 = ch.total_effect(3);
    f.spa271 = ch.total_effect(271);
    f.spa290 = ch.total_effect(290);
    f.spa228 = ch.total_effect(228);
    f.spa246 = ch.total_effect(246);
    f.spa14 = ch.total_effect(14);
    f.swimming_skill = static_cast<std::int32_t>(ch.adjusted_skill(kSwimming));
    f.safe_fall_adjusted = static_cast<std::int32_t>(ch.adjusted_skill(kSafeFall));
    const auto& skills = ch.sheet().skills;
    f.safe_fall_base = kSafeFall < skills.size() ? skills[kSafeFall] : 0;
    // ApplyFallDamage: dmg -= dmg * h / (h + 1000), h = the heroic AGI bonus (players with expansion bit 0x2000).
    f.heroic_agi = ch.heroic(&DerivedStats::heroic_agi);
    // CurrWeight is what ProcessAllStats stores (CalcCarriedWeight): the items' own weights, charm-scaled
    // (item_weight), bag reductions, less TotalEffect(221) percent.
    f.carried = ch.calc_carried_weight();
    f.strength = ch.stats().stats.str;
    f.stamina = ch.stats().stats.sta;
    // CalculateWeight reads Cur_HP / Max_HP with the spawn type as their argument.
    const int arg = static_cast<int>(ch.sheet().spawn_type);
    f.hp = ch.cur_hp(true, arg);
    f.max_hp = ch.max_hp(true, arg);
    f.low_level_exempt_off = ch.sheet().slowdown_any_level;
    // CacheBuffSlotEffects keys SPA 57 by Base2.
    f.levitate_mode2 = ch.cached_buff(57, 0);
    f.levitate_mode5 = ch.cached_buff(57, 1);
    const StatBlock& s = ch.stats().stats;
    f.sv_fire = s.sv_fire;
    f.sv_cold = s.sv_cold;
    f.sv_disease = s.sv_disease;
    return f;
}

} // namespace hailstone::game::mechanics
