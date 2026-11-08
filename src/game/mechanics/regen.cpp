// Per-tick regeneration: CharacterZoneClient::GetHPRegen @0x4527f0, GetManaRegen @0x452df0 and
// GetEnduranceRegen @0x448ec0 (checked against the original's vectors), with the
// x87 scalings of the spawn's regen modifiers through Fpu.
#include "game/mechanics/character.h"

#include <algorithm>

#include "game/spells.h"

namespace hailstone::game::mechanics {

namespace {

std::int32_t add(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
double d(std::int32_t v) { return static_cast<double>(v); }
double f(float v) { return static_cast<double>(v); }

// value * 100.0f * modifier * 0.01f + 0.5f, truncated
std::int32_t scale_by_modifier(const Fpu& fpu, std::int32_t value, float modifier) {
    double x = fpu.mul(d(value), 100.0);
    x = fpu.mul(x, f(modifier));
    x = fpu.mul(x, f(0.01f));
    x = fpu.add(x, 0.5);
    return Fpu::ftol(x);
}
// value * modifier + 0.5f, truncated
std::int32_t scale_half_up(const Fpu& fpu, std::int32_t value, float modifier) {
    return Fpu::ftol(fpu.add(fpu.mul(d(value), f(modifier)), 0.5));
}
// value * k, truncated
std::int32_t scale(const Fpu& fpu, std::int32_t value, float k) { return Fpu::ftol(fpu.mul(d(value), f(k))); }
// minutes * 1.5f * regen, truncated
std::int32_t scale_minutes(const Fpu& fpu, std::int32_t minutes, std::int32_t regen) {
    return Fpu::ftol(fpu.mul(fpu.mul(d(minutes), 1.5), d(regen)));
}
// _div(value, divisor).quot * 6; a zero divisor faults in the original and gives no fast regen here.
std::optional<std::int32_t> fast_regen(std::int32_t value, std::int32_t divisor) {
    if (divisor == 0 || (value == INT32_MIN && divisor == -1)) return std::nullopt;
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value / divisor) * 6u);
}

// The classes whose mana regen counts WIS rather than INT (the class table at 0x453210): cleric, paladin, ranger,
// druid, shaman and beastlord.
bool regen_uses_wisdom(std::uint8_t class_id) {
    switch (class_id) {
    case 2: case 3: case 4: case 6: case 10: case 15: return true;
    default: return false;
    }
}

// A monk's carried weight before the endurance penalty: the cap of the highest level band passed, else 30.
std::int32_t monk_weight_cap(std::int32_t level) {
    struct Band {
        std::int32_t above_level;
        std::int32_t cap;
    };
    static constexpr Band kBands[] = {{94, 57}, {89, 56}, {84, 55}, {79, 54}, {64, 53}, {63, 50}, {61, 47},
                                      {59, 45}, {54, 40}, {50, 38}, {44, 36}, {29, 34}, {14, 32}};
    for (const Band& band : kBands)
        if (level > band.above_level) return band.cap;
    return 30;
}

} // namespace

std::int32_t Character::hp_regen(bool include_items, bool* bleeding, bool no_rest) const {
    if (!sheet_.has_spawn) return 0;
    if (bleeding) *bleeding = false;
    if (sheet_.spawn_type != SpawnType::PC || !sheet_.has_owner) return 0;
    auto heroic_sta = [&] {
        if (hooks.heroic_sta_bonus)
            if (auto v = hooks.heroic_sta_bonus()) return *v / 20;
        return heroic(&DerivedStats::heroic_sta) / 20;
    };
    std::int32_t items = add(add(cached_item(0, 0), heroic_sta()), d_.hp_regen);
    std::int32_t level = sheet_.level;
    std::int32_t cap = 30;
    if (level > 60) cap = static_cast<std::int32_t>(static_cast<std::uint32_t>(level) - 30u);
    if (level > 65) cap = add(cap, static_cast<std::int32_t>(static_cast<std::uint32_t>(level) - 65u));
    cap = add(cap, total_effect(0x13d));
    cap = add(cap, heroic_sta());
    if (items > cap) items = cap;
    std::int32_t total = add(cached_aa(0, 0), items);
    std::int32_t regen = base_hp_regen();
    bool innate = sheet_.innate_3258 != 0xff;
    bool level_bonus = true; // +1 at 50 and 55, doubled
    bool have_level = false;
    if (sheet_.sitting) {
        if (level >= 50) {
            regen = add(regen, 1);
            if (level >= 65) regen = add(regen, 1);
        }
        if (sheet_.sitting_ms > 60000) {
            // No buff of spell category (+0x1c8, spells_us column 180) 0x2e on.
            bool no_regen_buff = true;
            for (int i = 0; i < max_effects(); ++i) {
                if (i >= static_cast<int>(sheet_.buffs.size())) continue;
                const SheetBuff& b = sheet_.buffs[static_cast<std::size_t>(i)];
                if (b.type == 0 || !valid_spell(b.spell_id)) continue;
                if (const SpellData* s = spells_ ? spells_(b.spell_id) : nullptr; s && s->global_group == 0x2e)
                    no_regen_buff = false;
            }
            if (no_regen_buff) {
                std::uint32_t minutes = sheet_.sitting_ms / 60000;
                level_bonus = false;
                if (minutes == 1) {
                    if (innate) level_bonus = true;
                    else regen = scale(fpu_, regen, 1.5f);
                } else {
                    regen = scale_minutes(fpu_, minutes > 9 ? 9 : static_cast<std::int32_t>(minutes), regen);
                    if (innate) regen = scale(fpu_, regen, 1.2f);
                }
            } else {
                level_bonus = false;
                if (innate) {
                    if (level >= 50) regen = add(regen, 1);
                    have_level = true;
                }
            }
        } else if (!innate) {
            level_bonus = false;
        }
    } else if (!innate) {
        level_bonus = false;
    }
    if (level_bonus) {
        if (level >= 50) regen = add(regen, 1);
        have_level = true;
    }
    if (have_level) {
        if (level >= 55) regen = add(regen, 1);
        regen = static_cast<std::int32_t>(static_cast<std::uint32_t>(regen) * 2u);
    }
    if (sheet_.tutorial && level < 21) regen = sheet_.sitting ? 10 : 6;
    if (sheet_.owner_food == 0 || sheet_.owner_drink == 0) regen = 0; // the owner's profile
    if (owner_leadership()) regen = add(regen, sheet_.leadership_hp_regen_bonus);
    if (sheet_.owner_status == 3) {
        if (cur_hp(true, static_cast<int>(sheet_.spawn_type)) < -5 || regen < 1) {
            regen = -1;
            if (bleeding) *bleeding = true;
        }
    }
    regen = scale_by_modifier(fpu_, regen, sheet_.hp_regen_modifier);
    if (add(total, regen) == 0 && cur_hp(true, 0) <= 0) regen = add(regen, -1);
    if (!no_rest && (sheet_.sitting || sheet_.can_med_on_horse) && sheet_.fast_regen_resting) {
        if (auto fast = fast_regen(max_hp(true, static_cast<int>(sheet_.spawn_type)), sheet_.fast_regen_hp_divisor);
            fast && regen <= *fast)
            regen = *fast;
    }
    return include_items ? add(total, regen) : regen;
}

std::int32_t Character::mana_regen(bool include_items, bool no_rest) const {
    std::int32_t regen = 0;
    std::int32_t level = sheet_.level;
    if (sheet_.has_owner) {
        if (sheet_.food == 0 || sheet_.drink == 0) {
            regen = 0;
        } else {
            regen = sheet_.mana_regen_standing;
            if (sheet_.can_med_on_horse || sheet_.spawn_sitting) {
                regen = sheet_.mana_regen_sitting;
                if (sheet_.class_id != 8 && adjusted_skill(0x1f) > 0) {
                    std::int32_t skill = skill_with_item_mods(0x1f, true);
                    regen = add(regen, 1);
                    if (skill > 1) regen = add(regen, 1);
                    if (skill > 0xe) regen = add(regen, skill / 15);
                }
            }
        }
        if (sheet_.tutorial && level < 21) regen = add(regen, sheet_.sitting ? 10 : 6);
        if (sheet_.is_pc()) {
            if (level > 0x3d) regen = add(regen, 1);
            if (level > 0x3f) regen = add(regen, 1);
        }
        if (include_items) {
            regen = add(regen, cached_aa(0xf, 0));
            regen = add(regen, cached_aa(0x1a1, 0));
            // GetPCSpellAffect(0x65): any buff whose spell has SPA 101 before its first blank effect.
            bool affected = false;
            if (hooks.has_spell_affect) {
                if (auto v = hooks.has_spell_affect(0x65)) affected = *v;
            } else {
                for (int i = 0; i < max_effects() && i < static_cast<int>(sheet_.buffs.size()) && !affected; ++i) {
                    const SheetBuff& b = sheet_.buffs[static_cast<std::size_t>(i)];
                    if (b.type == 0) continue;
                    if (const SpellData* s = spell(b.spell_id))
                        for (std::int32_t e : s->effect) {
                            if (e == 254) break;
                            if (e == 0x65) affected = true;
                        }
                }
            }
            if (affected) regen = add(regen, 1);
            std::int32_t items = cached_item(0xf, 0);
            std::int32_t more = cached_item(0x1a1, 0);
            items = add(add(items, d_.mana_regen), more);
            const std::int32_t caster_stat = regen_uses_wisdom(sheet_.class_id) ? heroic(&DerivedStats::heroic_wis)
                                                                                : heroic(&DerivedStats::heroic_int);
            std::int32_t stat = caster_stat / 25;
            items = add(items, stat);
            std::int32_t limit = add(add(total_effect(0x13e), stat), 15);
            if (items > limit) items = limit;
            if (items != 0) regen = add(regen, items);
        }
        if (level <= 0x46 && regen > 0x41) regen = 0x41;
    }
    if (sheet_.has_spawn) regen = scale_by_modifier(fpu_, regen, sheet_.mana_regen_modifier);
    if (!no_rest && (sheet_.sitting || sheet_.can_med_on_horse) && sheet_.has_owner && sheet_.fast_regen_resting) {
        if (auto fast = fast_regen(max_mana(true), sheet_.fast_regen_mana_divisor); fast && regen <= *fast) return *fast;
    }
    return regen;
}

std::int32_t Character::endurance_regen(bool include_items, bool no_rest) const {
    std::int32_t regen = base_endurance_regen();
    std::int32_t level = sheet_.level;
    if (sheet_.has_owner) {
        if ((sheet_.food == 0 || sheet_.drink == 0) && sheet_.spawn_type == SpawnType::PC) regen = 0;
        if (!sheet_.attacking && regen > 0) regen = add(regen, regen / 2);
        const std::int32_t weight_limit = sheet_.class_id == 7 ? monk_weight_cap(level) : d_.stats.str;
        const bool over = d_.weight > weight_limit;
        if (sheet_.running) regen = add(regen, level / -15);
        if (over) regen = add(regen, level / -15);
        if (include_items) {
            std::int32_t stats = add(add(add(heroic(&DerivedStats::heroic_sta), heroic(&DerivedStats::heroic_str)),
                                         heroic(&DerivedStats::heroic_dex)),
                                     heroic(&DerivedStats::heroic_agi)) / 4 / 50;
            std::int32_t items = add(d_.endurance_regen, stats);
            if (items > stats + 15) items = stats + 15;
            regen = add(regen, items);
        }
    }
    if (sheet_.has_spawn) regen = scale_half_up(fpu_, regen, sheet_.endurance_regen_modifier);
    std::int32_t aa = include_items ? cached_aa(0xbd, 0) : 0;
    if (!no_rest && (sheet_.sitting || sheet_.can_med_on_horse) && sheet_.has_owner && sheet_.fast_regen_resting) {
        if (auto fast = fast_regen(max_endurance(true), sheet_.fast_regen_endurance_divisor); fast && aa <= *fast)
            aa = *fast;
    }
    return add(aa, regen);
}

} // namespace hailstone::game::mechanics
