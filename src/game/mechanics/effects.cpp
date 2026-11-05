// The spell effect totals: CalcAffectChangeGeneric, the SpellCache (buffs,
// worn items, AAs, negations) and CharacterZoneClient::TotalEffect.
//
// Sources (rof2decomp):
//  - CalcAffectChangeGeneric @0x434df0: re/src/game/character/spell_effects.cpp (replaced, tested)
//  - CacheNegateSpellEffects @0x435d80: re/src/game/character/negate_effects_eh.cpp (replaced, tested)
//  - CacheAAEffects @0x4385d0: re/src/game/character/aa_effects_cache.cpp (replaced, tested)
//  - TotalEffect @0x438990: re/src/game/character/effects.cpp (replaced, tested)
//  - CacheBuffSlotEffects @0x436b30, CacheWornItemEffects @0x437780: NOT rewritten in rof2decomp yet; ported
//    from the exe's disassembly (tools/re/disasm.py) and the Ghidra export, notes/spell-stacking-and-stats.md §4.
#include "game/mechanics/character.h"

#include <algorithm>
#include <cmath>

#include "core/wrap_math.h"
#include "game/mechanics/item_stats.h"
#include "game/spells.h"

namespace nmsc::game::mechanics {

namespace {

constexpr std::int32_t kNegate = 382;     // SPA 0x17e NegateSpellEffect

// The original's integer math wraps; signed overflow is undefined in C++.
std::int32_t add(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
std::int32_t sub(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}
std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

bool valid_spell_id(std::int32_t id) { return id >= 1 && id <= 45000; } // IsValidSpellID @0x4ad150

// The SPAs cached per subindex = base2 (the buff list; the item list lacks 378).
bool keyed_buff(std::int32_t spa) {
    switch (spa) {
    case 57: case 169: case 183: case 184: case 185: case 186: case 197: case 216: case 220: case 224: case 227:
    case 247: case 260: case 262: case 264: case 267: case 268: case 330: case 378: case 418:
        return true;
    default:
        return false;
    }
}
bool keyed_item(std::int32_t spa) { return spa != 378 && keyed_buff(spa); }

// "A snare (negative) wins, else the largest" (buffs @0x43755b, items @0x43833c).
bool most_negative_kind(std::int32_t spa) {
    switch (spa) {
    case 3: case 169: case 170: case 171: case 172: case 173: case 174: case 175: case 176: case 177: case 178:
    case 179: case 180: case 184: case 185: case 186: case 188: case 195: case 292: case 200: case 114:
        return true;
    default:
        return false;
    }
}
// "Positive beats non-positive, else the smallest" (@0x437512, @0x4382f2).
bool positive_kind(std::int32_t spa) { return spa == 168 || spa == 182 || spa == 197; }
// The invisibility / see-invisible kinds: the largest (buffs) / the snare rule (items).
bool invis_kind(std::int32_t spa) {
    return spa == 12 || spa == 314 || spa == 28 || spa == 315 || spa == 29 || spa == 316 || spa == 13;
}
// The PvP resist debuffs scaled by 1.5 (@0x437372).
bool pvp_resist(std::int32_t spa) { return spa == 50 || spa == 49 || spa == 48 || spa == 46 || spa == 370 || spa == 47; }

// The AA cache's keyed SPAs and maxima (aa_effects_cache.cpp).
bool keyed_aa(std::int32_t spa) {
    switch (spa) {
    case 0x39: case 0xa9: case 0xb7: case 0xb8: case 0xb9: case 0xba: case 0xc5: case 0xd8: case 0xdc: case 0xe0:
    case 0xe3: case 0xf7: case 0x104: case 0x106: case 0x108: case 0x10b: case 0x10c: case 0x14a: case 0x1a2:
        return true;
    default:
        return false;
    }
}
bool aa_keeps_largest(std::int32_t spa) {
    return spa == 0x109 || spa == 0x1b7 || spa == 0x159 || spa == 0xd9 || spa == 0x15a || spa == 0x1b8 || spa == 0x116;
}
bool aa_keeps_largest_limit(std::int32_t spa) { return spa == 0x1b7 || spa == 0xd9 || spa == 0x1b8 || spa == 0x116; }

bool spell_affects(const SpellData& s, std::int32_t spa) {
    return std::find(s.effect.begin(), s.effect.end(), spa) != s.effect.end(); // EQ_Spell::SpellAffects @0x4af770
}

using Table = std::map<std::pair<std::int32_t, std::int32_t>, std::int32_t>;
std::int32_t get(const Table& t, std::int32_t spa, std::int32_t sub) { return EffectCache::get(t, spa, sub); }

} // namespace

Character::Character(const CharacterSheet& sheet, const GameTables& tables, SpellLookup spells)
    : sheet_(sheet), tables_(tables), spells_(std::move(spells)), fpu_(sheet.fpu), spawn_hp_max_(sheet.spawn_max_hp),
      spawn_hp_current_(sheet.spawn_hp), stored_hp_(sheet.stored_hp), encumbered_state_(sheet.encumbered_state) {}

const SpellData* Character::spell(std::int32_t id) const {
    if (!valid_spell_id(id) || !spells_) return nullptr;
    return spells_(id);
}

bool Character::valid_spell(std::int32_t id) const {
    if (hooks.is_valid_spell_id)
        if (auto v = hooks.is_valid_spell_id(id)) return *v;
    return valid_spell_id(id);
}

const net::Item* Character::walk_item(int index) const {
    if (index < kWornSlots) return sheet_.item(index);
    auto k = static_cast<std::size_t>(index - kWornSlots);
    return k < sheet_.extra_items.size() && sheet_.extra_items[k] ? &*sheet_.extra_items[k] : nullptr;
}

std::int32_t Character::cached_buff(std::int32_t spa, std::int32_t sub) const {
    if (hooks.buff_effect)
        if (auto v = hooks.buff_effect(spa, sub)) return *v;
    return get(effect_cache().buffs, spa, sub);
}
std::int32_t Character::cached_item(std::int32_t spa, std::int32_t sub) const {
    if (hooks.item_effect)
        if (auto v = hooks.item_effect(spa, sub)) return *v;
    return get(effect_cache().items, spa, sub);
}
std::int32_t Character::cached_aa(std::int32_t spa, std::int32_t sub) const {
    if (hooks.aa_effect)
        if (auto v = hooks.aa_effect(spa, sub)) return *v;
    return get(effect_cache().aas, spa, sub);
}

int Character::max_effects() const {
    if (hooks.max_effects)
        if (auto v = hooks.max_effects()) return static_cast<std::uint8_t>(*v);
    if (!sheet_.has_spawn) return 42;
    if (sheet_.spawn_type == SpawnType::NPC && !sheet_.is_mercenary) return 97;
    if (sheet_.spawn_type == SpawnType::PC) return 63;
    return sheet_.is_mercenary ? 63 : 42;
}

// ---- CalcAffectChangeGeneric @0x434df0 ----

std::int32_t Character::calc_affect_change(const SpellData& spell, std::uint32_t level, int slot,
                                           const SheetBuff* affect, std::int32_t base, bool cap_at_max) const {
    if (slot < 0 || slot > 11) return 0; // the original asserts (int3)
    const auto k = static_cast<std::size_t>(slot);
    std::int32_t value = base < 0 ? static_cast<std::int32_t>(0u - u(base)) : base;
    std::uint32_t lvl = level & 0xff;
    std::uint32_t change = 0;
    std::int32_t calc = spell.formula[k];
    std::int32_t spa = spell.effect[k];
    std::int32_t maxv = spell.max[k];
    bool decaying = false;
    auto ticks_used = [&] {
        return sub(spell_duration(spell, static_cast<std::uint8_t>(level)), affect ? affect->duration : 0);
    };
    if (spa != 32 && calc > 1000 && calc < 2000) {
        if (spell.duration_formula != 0 && affect) {
            change = u(calc - 1000) * u(ticks_used());
            if (value >= 0) change = 0u - change;
        }
        decaying = true;
    } else if (spa != 32 && calc > 2000 && calc < 3000) {
        change = u(calc - 2000) * lvl;
        decaying = true;
    }
    if (!decaying) {
        auto over_level = [&](std::uint32_t floor, std::uint32_t factor) {
            int over = static_cast<int>(lvl) - static_cast<int>(floor);
            if (over > 0) change = u(over) * factor;
        };
        auto over_duration = [&](std::uint32_t factor) {
            if (spell.duration_formula != 0 && affect) {
                change = u(ticks_used()) * factor;
                if (value >= 0) change = 0u - change;
            }
        };
        switch (calc) {
        case 100: change = 0; break;
        case 101: change = lvl >> 1; break;
        case 102: change = lvl; break;
        case 103: change = lvl * 2; break;
        case 104: change = lvl * 3; break;
        case 105: change = lvl * 4; break;
        case 107: over_duration(1); break;
        case 108: over_duration(2); break;
        case 109: change = lvl >> 2; break;
        case 110: change = lvl / 6; break;
        case 111: over_level(16, 6); break;
        case 112: over_level(24, 8); break;
        case 113: over_level(34, 10); break;
        case 114: over_level(44, 15); break;
        case 115: over_level(15, 7); break;
        case 116: over_level(24, 10); break;
        case 117: over_level(34, 13); break;
        case 118: over_level(44, 20); break;
        case 119: change = lvl >> 3; break;
        case 120: over_duration(5); break;
        case 121: change = lvl / 3; break;
        case 122: over_duration(12); break;
        case 123:
            // RandomRoll1ToN(max - value): random in the original; no fixed result to reproduce. 0 here.
            change = 0;
            break;
        case 124: over_level(50, 1); break;
        case 125: over_level(50, 2); break;
        case 126: over_level(50, 3); break;
        case 127: over_level(50, 4); break;
        case 128: over_level(50, 5); break;
        case 129: over_level(50, 10); break;
        case 130: over_level(50, 15); break;
        case 131: over_level(50, 20); break;
        case 132: over_level(50, 25); break;
        case 137: // scales with the character's missing HP
            if (sheet_.has_spawn && spawn_hp_max_ != 0)
                change = 0u - u(core::wrapped_div(u(spawn_hp_current_) * u(value), spawn_hp_max_));
            break;
        case 138:
            if (sheet_.has_spawn) {
                std::int32_t half = spawn_hp_max_ / 2;
                if (spawn_hp_current_ > half) change = 0u - u(value);
                else if (half != 0)
                    change = 0u - u(core::wrapped_div(u(spawn_hp_current_) * u(value), half));
            }
            break;
        case 139:
            if (lvl > 30) change = u(static_cast<std::int32_t>(lvl - 30) / 2);
            break;
        case 140:
            if (lvl > 30) change = lvl - 30;
            break;
        case 141:
            if (lvl > 30) change = u(core::wrapped_div(lvl * 3 - 90, 2));
            break;
        case 142:
            if (lvl > 30) change = lvl * 2 - 60;
            break;
        case 143: change = (lvl * 3) >> 2; break;
        default: change = lvl * u(calc); break;
        }
    }
    if (spa == 11 && value < 100) {
        value = static_cast<std::int32_t>(u(value) - change);
        if (maxv != 0 && value < maxv) value = maxv;
    } else {
        value = static_cast<std::int32_t>(u(value) + change);
    }
    if (cap_at_max && maxv != 0) {
        std::int32_t cap = maxv < 0 ? static_cast<std::int32_t>(0u - u(maxv)) : maxv;
        if (cap < value) value = cap;
    }
    if (base >= 0) return value; // the original's (calc >= 0 || calc < 0x85) is always true
    return static_cast<std::int32_t>(0u - u(value));
}

// ---- CalcAffectChange @0x436670's distance modifier (rof2decomp batch 283, re/src/game/character/affect_change.cpp) ----

double Character::spell_distance_mod(const SpellData& spell, std::optional<float> distance) const {
    const float max_dist = spell.distance_mod_end_x;
    const float min_dist = spell.distance_mod_start_x;
    if (max_dist == 0.0f && min_dist == 0.0f) return 1.0;
    if (!distance) return spell.distance_mod_end_mod;
    float d = *distance;
    if (d > max_dist) d = max_dist;
    if (d < min_dist) d = min_dist;
    double r = fpu_.add(fpu_.mul(fpu_.sub(d, min_dist), spell.distance_mod_ratio), spell.distance_mod_start_mod);
    return r < 0.0 ? 0.0 : r;
}

std::int32_t Character::scale_by_distance(const SpellData& spell, std::int32_t value, std::optional<float> distance) const {
    if (spell.distance_mod_end_x == 0.0f && spell.distance_mod_start_x == 0.0f) return value;
    if (!sheet_.has_spawn) return value;
    // fild, fstp dword: the value as a float; the modifier times it, compared with 0 (NaN is not below), then
    // ceil(x - 0.5) below 0, else floor(x + 0.5), and __ftol2_sse.
    const double scale = Fpu::to_float(static_cast<double>(value));
    const double x = fpu_.mul(spell_distance_mod(spell, distance), scale);
    const std::int32_t r = x < 0.0 ? Fpu::ftol(std::ceil(fpu_.sub(x, 0.5))) : Fpu::ftol(std::floor(fpu_.add(x, 0.5)));
    if (value < 0 && r == 0) return -1;
    return r;
}

// ---- the SpellCache ----

const EffectCache& Character::effect_cache() const {
    if (!cache_) cache_ = build_cache();
    return *cache_;
}

std::uint32_t Character::negate_mask(std::int32_t spa) const {
    const EffectCache& c = effect_cache();
    return static_cast<std::uint32_t>(get(c.buffs, kNegate, spa) | get(c.items, kNegate, spa) | get(c.aas, kNegate, spa));
}

// ReCacheSpellEffects @0x438910 (buffs and items) plus CacheAAEffects @0x4385d0, in their order: the negations
// first, then every buff slot, the worn items, and the AAs.
EffectCache Character::build_cache() const {
    EffectCache c;
    cache_negates(c);
    for (int slot = 0; slot < max_effects(); ++slot) cache_buff_slot(c, slot);
    cache_worn_items(c);
    cache_aas(c);
    return c;
}


// CacheNegateSpellEffects @0x435d80: SPA 382 values (base, 0xff when 0) or'ed together under the negated SPA (the
// effect's base2). Buffs (all used slots), the worn spells of the 44 item lookups (worn slots 0..22 and the 21
// extra items; not lookup 22, the ammo slot; only the item itself: the original repeats the same item instead of
// stepping to its augments), and owned AAs.
void Character::cache_negates(EffectCache& c) const {
    auto add_spell = [](Table& t, const SpellData& s) {
        for (std::size_t j = 0; j < 12; ++j) {
            if (s.effect[j] != kNegate) continue;
            std::int32_t key = s.base2[j];
            std::int32_t base = s.base[j] == 0 ? 0xff : s.base[j];
            t[{kNegate, key}] = base | get(t, kNegate, key);
        }
    };
    for (int slot = 0; slot < max_effects(); ++slot) {
        c.buffs[{kNegate, kNegate}] = 0xff;
        if (slot >= static_cast<int>(sheet_.buffs.size())) continue;
        const SheetBuff& b = sheet_.buffs[static_cast<std::size_t>(slot)];
        if (b.type == 0 || !valid_spell(b.spell_id)) continue;
        if (const SpellData* s = spell(b.spell_id)) add_spell(c.buffs, *s);
    }
    c.items[{kNegate, kNegate}] = 0xff;
    if (sheet_.has_spawn && (sheet_.spawn_type == SpawnType::PC || sheet_.is_pet)) {
        for (int k = 0; k < kWornSlots + 21; ++k) {
            const net::Item* item = walk_item(k);
            if (!item || item->item_class != 0 || static_cast<std::int32_t>(item->scaled_value) < 0) continue;
            // the use check applies to the worn possessions slots only
            if (sheet_.spawn_type == SpawnType::PC && k < kWornSlots && !can_use_item(*item)) continue;
            std::int32_t id = item->effects[net::kEffectWorn].effect;
            if (id < 2 || !valid_spell_id(id)) continue;
            const SpellData* s = spell(id);
            if (!s || s->duration_formula == 0 || k == net::kSlotAmmo) continue;
            add_spell(c.items, *s);
        }
    }
    c.aas[{kNegate, kNegate}] = 0xff;
    for (const SheetAbility& a : sheet_.abilities) {
        if (!(a.class_mask & (1u << (sheet_.class_id & 31)))) continue;
        if (a.required_level > sheet_.level && !a.ignores_level) continue;
        for (const SheetAAEffect& e : a.effects) {
            if (e.spa != kNegate) continue;
            std::int32_t base = e.base == 0 ? 0xff : e.base;
            c.aas[{kNegate, e.limit}] = base | get(c.aas, kNegate, e.limit);
        }
    }
}

// CharacterZoneClient_CacheBuffSlotEffects @0x436b30 (2830 bytes; from the disassembly).
void Character::cache_buff_slot(EffectCache& c, int slot) const {
    if (slot >= static_cast<int>(sheet_.buffs.size())) return;
    const SheetBuff& b = sheet_.buffs[static_cast<std::size_t>(slot)];
    if (b.type == 0 || !valid_spell(b.spell_id)) return;
    const SpellData* sp = spell(b.spell_id);
    if (!sp) return;
    const SpellData& s = *sp;
    // A held countdown (Spell_IsBuffCountdownHeld @0x4dcf20, game::buff_countdown_held) costs no upkeep.
    if (!buff_countdown_held(&s, sheet_.buff_timers_on_hold))
        c.endurance_upkeep = add(c.endurance_upkeep, s.endurance_upkeep);
    for (int i = 0; i < 12; ++i) {
        const auto k = static_cast<std::size_t>(i);
        std::int32_t spa = s.effect[k];
        std::uint32_t mask = static_cast<std::uint32_t>(get(c.buffs, kNegate, spa) | get(c.items, kNegate, spa) |
                                                        get(c.aas, kNegate, spa));
        if (spa == kNegate || (s.spell_type >= 1 && (mask & 1))) continue;
        std::int32_t key = keyed_buff(spa) ? s.base2[k] : 0;
        std::int32_t limit_old = 0, limit_new = 0;
        if (spa == 408 || spa == 409 || spa == 410) {
            limit_old = get(c.buffs, spa, 1);
            limit_new = calc_affect_change(s, b.level, i, &b, s.base2[k], true); // CalcAffectChangeBase2 @0x436640
        }
        std::int32_t old = get(c.buffs, spa, key);
        std::int32_t v = calc_affect_change(s, b.level, i, &b, s.base[k], true);
        // CalcAffectChange inlined (@0x436d63): the slot's BuffCasterIDs spawn (GetSpawnByID) scales v by its
        // distance (the far modifier when it is not found).
        v = scale_by_distance(s, v, b.caster_distance);
        auto set = [&](std::int32_t value) { c.buffs[{spa, key}] = value; };
        if (spa == 220 || spa == 418) {
            if (v > old) set(v);
            continue;
        }
        if (spa == 367) { // SetBodyType: the first one
            if (old == 0) set(v);
            continue;
        }
        if (spa == 11) { // haste: slows (below 100) keep the lowest, hastes the highest
            if (old < 100) {
                if (v < old || old == 0) set(v);
            } else if (v < 100 || v > old) {
                set(v);
            }
            continue;
        }
        if (spa == 408 || spa == 409 || spa == 410) { // the smallest non-zero limits
            if ((old > v || old == 0) && v != 0) c.buffs[{spa, 0}] = v;
            if ((limit_old > limit_new || limit_old == 0) && limit_new != 0) c.buffs[{spa, 1}] = limit_new;
            continue;
        }
        if (spa == 98 || spa == 119 || invis_kind(spa) || spa == 265) {
            if (v > old) set(v);
            continue;
        }
        if (spa == 424) continue; // physics effects (MarkPhysicsEffectUpdated), not cached
        if (b.modifier != 1.0f && spa != 182) {
            // fild v / fmul dword modifier / __ftol2_sse
            v = Fpu::ftol(fpu_.mul(static_cast<double>(v), static_cast<double>(b.modifier)));
        }
        if (most_negative_kind(spa)) {
            if (old < 0) {
                if (v < old) set(v);
            } else if (v < 0 || v > old) {
                set(v);
            }
            continue;
        }
        if (positive_kind(spa)) {
            if (old > 0) {
                if (v > old) set(v);
            } else if (v > 0 || v < old) {
                set(v);
            }
            continue;
        }
        if (sheet_.pvp_ruleset && sheet_.is_pc() && v < 0 && pvp_resist(spa)) v = add(v, v / 2);
        if (b.type == 4) { // an inverted buff takes its value off
            set(sub(old, v));
            continue;
        }
        if (spa == 0 && v < 0 && sheet_.is_pc() && b.caster_is_pc) {
            v = core::wrapped_mul_div(v, b.caster_is_self ? 75 : 70, 100);
            if (v > -1) v = -1;
        }
        set(add(old, v));
    }
}

// CharacterZoneClient_CacheWornItemEffects @0x437780 (3428 bytes; from the disassembly): the worn spells
// (GetSpellID(2)) of the 44 item lookups (worn slots 0..22, then sheet.extra_items) and their augments, for
// players, pets and mercenaries.
void Character::cache_worn_items(EffectCache& c) const {
    if (!sheet_.has_spawn || !(sheet_.spawn_type == SpawnType::PC || sheet_.is_pet || sheet_.is_mercenary)) return;
    for (int k = 0; k < kWornSlots + 21; ++k) {
        const net::Item* top = walk_item(k);
        if (!top || top->item_class != 0 || !can_use_item(*top)) continue;
        int children = 0;
        for (const auto& bi : top->contents) children = std::max(children, static_cast<int>(bi.index) + 1);
        const net::Item* current = top;
        for (int j = 0; j <= children; ++j) {
            bool step = true;
            if (current && current->item_class == 0) {
                step = false;
                do {
                    if (static_cast<std::int32_t>(current->scaled_value) < 0) break; // GetStatScalePercent
                    if (sheet_.spawn_type == SpawnType::PC && current == top && k < kWornSlots &&
                        !can_use_item(*current))
                        break;
                    std::int32_t id = current->effects[net::kEffectWorn].effect;
                    if (id < 2) {
                        step = true;
                        break;
                    }
                    const SpellData* sp = spell(id);
                    if (!sp || sp->duration_formula == 0) break;
                    if (k == net::kSlotAmmo && !spell_affects(*sp, 0x42) && !spell_affects(*sp, 0x41)) break;
                    // ItemBase_GetSpellEffectiveCasterLevel, scaled by the charm percent, then by the socket's
                    // aug stat percent for an augment.
                    std::int32_t level = current->effects[net::kEffectWorn].level;
                    if (current->charm_file_id != 0) level = core::wrapped_mul_div(static_cast<std::int32_t>(current->scaled_value), level, 100);
                    if (j > 0) level = apply_aug_stat_percent(*top, j - 1, level, true);
                    const SpellData& s = *sp;
                    for (int i = 0; i < 12; ++i) {
                        const auto ki = static_cast<std::size_t>(i);
                        std::int32_t spa = s.effect[ki];
                        std::uint32_t mask = static_cast<std::uint32_t>(
                            get(c.buffs, kNegate, spa) | get(c.items, kNegate, spa) | get(c.aas, kNegate, spa));
                        if (spa == kNegate || (mask & 2)) continue;
                        std::int32_t key = keyed_item(spa) ? s.base2[ki] : 0;
                        std::int32_t v = calc_affect_change(s, static_cast<std::uint32_t>(level), i, nullptr, s.base[ki], true);
                        // CalcAffectChangeGeneric only (@0x4370fd): worn effects have no distance modifier.
                        std::int32_t old = get(c.items, spa, key);
                        auto set = [&](std::int32_t value) { c.items[{spa, key}] = value; };
                        if (most_negative_kind(spa) || invis_kind(spa)) {
                            if (old < 0) {
                                if (v < old) set(v);
                            } else if (v < 0 || v > old) {
                                set(v);
                            }
                        } else if (positive_kind(spa)) {
                            if (old > 0) {
                                if (v > old) set(v);
                            } else if (v > 0 || v < old) {
                                set(v);
                            }
                        } else if (spa == 59) { // damage shield: summed, at least -15
                            std::int32_t sum = add(old, v);
                            set(sum < -15 ? -15 : sum);
                        } else if (spa == 11) {
                            if (sheet_.level < 26 && v > 110) v = 110;
                            if (old > 99) {
                                if (v < 100 || v > old) set(v);
                            } else if (v < old || old == 0) {
                                set(v);
                            }
                        } else if (spa == 265) {
                            if (v > old) set(v);
                        } else {
                            set(add(old, v));
                        }
                    }
                    step = true;
                } while (false);
            }
            if (step && j < children) {
                current = top->content(static_cast<std::uint32_t>(j));
            }
        }
    }
}

// CharacterZoneClient_CacheAAEffects @0x4385d0.
void Character::cache_aas(EffectCache& c) const {
    for (const SheetAbility& a : sheet_.abilities) {
        if (!(a.class_mask & (1u << (sheet_.class_id & 31)))) continue;
        if (a.required_level > sheet_.level && !a.ignores_level) continue;
        for (const SheetAAEffect& e : a.effects) {
            std::uint32_t mask = static_cast<std::uint32_t>(get(c.aas, kNegate, e.spa) | get(c.buffs, kNegate, e.spa) |
                                                            get(c.items, kNegate, e.spa));
            if (mask & 4) continue;
            std::int32_t key = keyed_aa(e.spa) ? e.limit : 0;
            if (aa_keeps_largest(e.spa)) {
                if (e.base > get(c.aas, e.spa, key)) c.aas[{e.spa, key}] = e.base;
            } else {
                c.aas[{e.spa, key}] = add(get(c.aas, e.spa, key), e.base);
            }
            if (aa_keeps_largest_limit(e.spa) && e.limit > get(c.aas, e.spa, 1)) c.aas[{e.spa, 1}] = e.limit;
        }
    }
}

// ---- TotalEffect @0x438990 ----

std::int32_t Character::total_haste(std::int32_t best_haste, std::int32_t item_haste, std::int32_t spell_haste,
                                    std::int32_t haste2, std::int32_t haste3) const {
    std::int32_t value = spell_haste;
    if ((best_haste < value && best_haste > 0) || value == 0) value = best_haste;
    if (static_cast<std::uint32_t>(value) - 1u <= 99) return value; // a slow: percent of normal speed
    std::int32_t total = add(item_haste, spell_haste);
    std::int32_t cap = add(sheet_.level, 125);
    std::int32_t overhaste = haste3;
    if (overhaste > 0) {
        if (overhaste > 100) overhaste -= 100;
        if (sheet_.level > 50) {
            if (overhaste > 25) overhaste = 25;
        } else if (overhaste > 10) {
            overhaste = 10;
        }
        cap = add(cap, overhaste);
    }
    if (spell_haste >= 100 && item_haste >= 100) total = add(total, -100);
    if (haste2 > 100 && sheet_.level > 49) {
        std::int32_t bard = haste2 > 110 ? 110 : haste2;
        if (total != 0) total = add(total, -100);
        total = add(total, bard);
    }
    if (sheet_.level > 50) cap = add(overhaste, 185);
    if (sheet_.level > 59) cap = add(overhaste, 200);
    if (sheet_.has_spawn && sheet_.spawn_type == SpawnType::NPC) {
        if (!sheet_.summoned) {
            cap = add(overhaste, 250);
        } else { // a pet: level + 110, plus the master's levels above 39 and above 60
            cap = add(add(sheet_.level, overhaste), 110);
            if (sheet_.is_pet && sheet_.master_level >= 0) {
                std::int32_t above39 = sheet_.master_level - 39;
                if (above39 > 0) cap = add(cap, above39);
                std::int32_t above60 = sheet_.master_level - 60;
                if (above60 > 0) cap = add(cap, above60);
            }
        }
    }
    if (total != 0) total = add(total, overhaste);
    else if (overhaste != 0) total = add(overhaste, 100);
    return total > cap ? cap : total;
}

std::int32_t Character::total_effect(std::int32_t spa, bool include_items, std::int32_t subindex, bool include_aa,
                                     bool include_buffs) const {
    if (hooks.total_effect)
        if (auto v = hooks.total_effect(spa, include_items, subindex, include_aa, include_buffs)) return *v;
    if (spa == 15 || spa == 417) {
        std::uint8_t cls = sheet_.class_id;
        if (cls == 7 || cls == 9 || cls == 1 || cls == 16) return 0; // no mana effects for pure melee classes
    }
    std::int32_t total = 0;
    if (include_buffs) total = cached_buff(spa, subindex);
    if (include_aa) total = add(total, cached_aa(spa, subindex));
    std::int32_t spell_haste = 0, haste2 = 0, haste3 = 0, buff_speed = 0;
    if (spa == 11) {
        spell_haste = cached_buff(11, 0);
        haste2 = cached_buff(98, 0);
        haste3 = cached_buff(119, 0);
        std::int32_t slow = cached_buff(371, 0);
        if (slow != 0) {
            if (spell_haste == 0 || spell_haste >= 100) spell_haste = sub(spell_haste == 0 ? 100 : spell_haste, slow);
            else if (spell_haste >= sub(100, slow)) spell_haste = sub(100, slow);
            if (spell_haste <= 0) spell_haste = 1;
        }
    } else if (spa == 3) {
        buff_speed = cached_buff(3, 0);
    }
    bool items_count = include_items && sheet_.has_spawn &&
                       (sheet_.spawn_type == SpawnType::PC || sheet_.is_pet || sheet_.is_mercenary);
    std::int32_t item_speed = 0;
    if (items_count) {
        if (spa == 11) {
            std::int32_t item_haste = include_buffs ? d_.attack_speed : d_.attack_speed_no_buffs;
            return total_haste(item_haste, item_haste, spell_haste, haste2, haste3);
        }
        if (spa == 3) item_speed = cached_item(3, 0);
        else if (spa == 15 || spa == 417) total = add(total, cached_item(spa, 0));
        else total = add(total, cached_item(spa, subindex));
    }
    if (spa == 11) return total_haste(999, 0, spell_haste, haste2, haste3);
    if (spa == 3) {
        if (buff_speed < 0 || item_speed < 0) return buff_speed < item_speed ? buff_speed : item_speed;
        if (buff_speed <= 0 && item_speed <= 0) return 0;
        return item_speed > buff_speed ? item_speed : buff_speed;
    }
    bool largest_only = (spa >= 12 && spa <= 13) || (spa >= 28 && spa <= 29) || (spa >= 314 && spa <= 316);
    if (!largest_only) return total;
    std::int32_t from_buffs = include_buffs ? cached_buff(spa, 0) : 0;
    std::int32_t from_items = include_items ? cached_item(spa, 0) : 0;
    std::int32_t from_aa = include_aa ? cached_aa(spa, 0) : 0;
    std::int32_t best = from_buffs > from_items ? from_buffs : from_items;
    best = best > from_aa ? best : from_aa;
    return best >= 3000 ? 3000 : best;
}

std::int32_t total_effect(const CharacterSheet& sheet, const GameTables& tables, const SpellLookup& spells,
                          std::int32_t spa, bool include_items, std::int32_t subindex, bool include_aa,
                          bool include_buffs) {
    Character ch(sheet, tables, spells);
    if (spa == 11) ch.process_all_stats(); // haste needs the item haste
    return ch.total_effect(spa, include_items, subindex, include_aa, include_buffs);
}

} // namespace nmsc::game::mechanics
