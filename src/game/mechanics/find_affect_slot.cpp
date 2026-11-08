// CharacterZoneClient::FindAffectSlot and its stacking rules (game/mechanics/find_affect_slot.h). The main
// function keeps the block structure of rof2decomp's rewrite (its labels carry the original's addresses), so every
// check runs in the original's order.
#include "game/mechanics/find_affect_slot.h"

namespace hailstone::game::mechanics::affect {

namespace {

constexpr int kSpellSlots = 12;
constexpr std::int32_t kEndOfEffects = 0xfe;
constexpr std::int32_t kStackingBlock = 0x94;

std::int32_t spa_at(const Spell* s, std::uint32_t j) { return s->attrib[j]; }
std::int32_t base_at(const Spell* s, std::uint32_t j) { return s->base[j]; }

bool affects(const Spell* s, std::int32_t spa) { return hit::spell_affects(s, spa) != 0; }

bool valid_spell(std::int32_t id) { return hit::is_valid_spell_id(static_cast<std::uint32_t>(id)); }

// EQ_Affect::GetSlotDataValue @0x7e3de0.
std::int32_t slot_data_value(const Affect* a, std::uint32_t index) {
    return index > 0xb ? 0 : a->slot_data[index];
}

// 0x1b - |SPA 0x147 (buff + AA + item caches)| - the character's virtual +0x2c, wrapping (GetLastEffectSlot).
std::int32_t locked_slots(Character* self, AffectQueries& q) {
    if (!self->cache_valid) q.recache_spell_effects(self);
    auto value = static_cast<std::uint32_t>(q.cached_buff_effect(self, 0x147));
    value += static_cast<std::uint32_t>(q.cached_aa_effect(self, 0x147));
    Spawn* spawn = self->spawn;
    if (spawn && (spawn->type == 0 || spawn->owner_38c != 0 || spawn->flag_208 != 0))
        value += static_cast<std::uint32_t>(q.cached_item_effect(self, 0x147));
    std::uint32_t extra = q.extra_buff_slots();
    std::uint32_t magnitude = static_cast<std::int32_t>(value) < 0 ? 0u - value : value;
    return static_cast<std::int32_t>(0x1bu - magnitude - extra);
}

} // namespace

std::uint32_t pc_extra_buff_slots(std::int32_t level) { return (level > 0x47 ? 1u : 0u) + (level > 0x4a ? 1u : 0u); }

bool is_spa_stacking(std::int32_t spa) { return spa >= 0x1be && spa <= 0x1c1; }

bool is_spa_ignored_by_stacking(std::int32_t spa) {
    // The two switch tables of the original, read from the exe (EQ_Spell_StackingSpaTables.jsonl agrees).
    switch (spa) {
    case 13: case 35: case 36: case 57: case 65: case 66: case 79: case 116: case 148: case 149: case 167: case 220:
    case 235: case 254: case 286: case 287: case 296: case 297: case 302: case 303: case 310: case 311: case 335:
    case 340: case 348: case 369: case 374: case 382: case 385: case 411: case 412: case 414: case 415: case 418:
        return true;
    default:
        return (spa >= 124 && spa <= 144) || (spa >= 391 && spa <= 396) || (spa >= 420 && spa <= 425);
    }
}

bool is_stackable_dot(const Spell& s) {
    if (s.not_stackable_dot != 0 || s.spell_type != 0 || s.duration_formula == 0) return false;
    return affects(&s, 0) || affects(&s, 0x1a8);
}

bool is_click_off_allowed(const Spell& s) { return s.spell_type < 1 || s.no_remove != 0; }

bool is_res_effect_spell(std::int32_t id) { return id == 0x2f4 || id == 0x331f || id == 0x1481; }

std::uint8_t spell_level_needed(const Spell& s, std::int32_t class_id) {
    if (static_cast<std::uint32_t>(class_id) - 1u > 34) return s.class_level[1];
    return s.class_level[static_cast<std::size_t>(class_id)];
}

std::uint32_t bard_cast_bard(const Spell* s, std::uint32_t class_id) {
    if (!s || class_id != 8 || s->is_skill != 0) return 0;
    std::uint8_t level = spell_level_needed(*s, 8);
    return level != 0 && level < 0xff ? 1 : 0;
}

std::uint8_t max_effects(const Character* self) {
    if (!self || !self->spawn) return 42;
    const Spawn* me = self->spawn;
    if (me->type == 1 && me->flag_208 == 0) return 97;
    if (me->type == 0) return 63;
    return me->flag_208 != 0 ? 63 : 42;
}

Affect* get_effect(std::int32_t slot, AffectQueries& q) {
    return q.effect_at(slot >= 0 && slot < kAffectSlots ? slot : 0);
}

std::int32_t last_effect_slot(Character* self, std::uint32_t first, std::uint32_t second, std::uint32_t third,
                              AffectQueries& q) {
    Spawn* spawn = self->spawn;
    if (!spawn) return 0x2a - 1;
    if (spawn->type != 0 && spawn->flag_208 == 0) {
        if (spawn->type == 1) return 0x61 - 1;
        return 0x2a - 1;
    }
    if (static_cast<std::uint8_t>(first)) return max_effects(self) - 2;
    if (static_cast<std::uint8_t>(second)) return max_effects(self) - 1;
    if (static_cast<std::uint8_t>(third)) return max_effects(self) - 0x16;
    std::int32_t locked = locked_slots(self, q);
    locked = locked < 0 ? 0 : locked_slots(self, q);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(max_effects(self)) - static_cast<std::uint32_t>(locked) -
                                     0x16u);
}

std::int32_t open_effect_slot(Character* self, std::uint32_t short_only, std::uint32_t song, std::int32_t fallback,
                              AffectQueries& q) {
    Spawn* spawn = self->spawn;
    std::int32_t first = 0;
    if (spawn && (spawn->type == 0 || spawn->flag_208 != 0)) {
        if (static_cast<std::uint8_t>(short_only)) first = max_effects(self) - 0x15;
        else if (static_cast<std::uint8_t>(song)) first = max_effects(self) - 1;
    }
    std::int32_t last = last_effect_slot(self, short_only, song, 0, q);
    if (fallback <= last && fallback >= first) return fallback;
    for (std::int32_t i = first; i <= last; ++i)
        if (get_effect(i, q)->type == 0) return i;
    if (static_cast<std::uint8_t>(short_only) && !static_cast<std::uint8_t>(song)) {
        last = last_effect_slot(self, 0, 0, 0, q);
        for (std::int32_t i = 0; i <= last; ++i)
            if (get_effect(i, q)->type == 0) return i;
    }
    return fallback;
}

bool is_stack_blocked(Character* self, const Spell* spell, std::uint8_t caster_class, AffectQueries& q) {
    if (!spell || !self->spawn) return false;
    if (caster_class == 8 && spell->is_skill == 0 && spell_level_needed(*spell, 8) != 0 &&
        spell_level_needed(*spell, 8) < 0xff)
        return false;
    const int count = max_effects(self);
    for (int i = 0; i < count; ++i) {
        const Affect* buff = get_effect(i, q);
        const Spell* other = q.spell_by_id(buff->spell_id);
        if (!other) continue;
        if (buff->spell_id == spell->id && spell->stacks_with_self == 0) continue;
        for (std::uint32_t j = 0; j < kSpellSlots; ++j) {
            std::int32_t effect = other->attrib[j];
            if (effect == kEndOfEffects) break;
            if (effect != kStackingBlock) continue;
            std::int32_t blocked = other->base[j];
            std::int32_t at = other->calc[j];
            if (at > 200) at -= 201;
            std::int32_t mx = other->max[j];
            auto limit = static_cast<std::int32_t>(mx < 0 ? 0u - static_cast<std::uint32_t>(mx) : static_cast<std::uint32_t>(mx));
            if (static_cast<std::uint32_t>(at) >= kSpellSlots) continue; // the original's int3
            auto k = static_cast<std::size_t>(at);
            if (spell->attrib[k] != blocked) continue;
            std::int32_t value = spell->max[k] != 0 ? spell->max[k] : spell->base[k];
            if (value > limit) continue;
            for (std::size_t n = 0; n < kSpellSlots; ++n)
                if (spell->attrib[n] == kStackingBlock && spell->base[n] == blocked &&
                    static_cast<std::int32_t>(static_cast<std::uint32_t>(spell->calc[n]) - 0xc9u) == at)
                    value = spell->max[n];
            if (value <= limit) return true;
        }
    }
    return false;
}

Affect* find_affect_slot(Character* self, std::int32_t spell_id, Spawn* caster, std::int32_t* slot, std::uint32_t keep,
                         std::uint32_t caster_level, AffectQueries& q) {
    *slot = -1;
    auto level = static_cast<std::int32_t>(caster_level); // Lc
    std::int32_t limit = 0;
    std::uint8_t found = 0;
    std::uint8_t dot = 0;              // the caster level argument's low byte, reused as a flag
    std::uint32_t index = 0;           // the same argument as the main loop's buff index
    std::uint8_t short_only = 0, song = 0; // the buffs / caster arguments' low bytes at find_open
    std::int32_t existing_id = 0;
    const Spell* existing = nullptr;
    Affect* current = nullptr;
    std::uint32_t j = 0;
    std::int32_t i = 0;
    std::int32_t old_change = 0, new_change = 0;
    Affect* effect = nullptr;
    std::int32_t result = 0;
    if (!caster) return nullptr;
    if (!valid_spell(spell_id)) return nullptr;
    const Spell* spell = q.spell_by_id(spell_id);
    if (!spell) return nullptr;
    if (static_cast<std::int32_t>(caster_level) <= 0) {
        level = q.get_level(caster);
        if (bard_cast_bard(spell, caster->class_id) != 0) {
            if (Character* pc = caster->character) {
                if (pc->cache_valid == 0) q.recache_spell_effects(pc);
                auto extra = static_cast<std::uint32_t>(q.cached_buff_effect(pc, 0x110));
                extra += static_cast<std::uint32_t>(q.cached_aa_effect(pc, 0x110));
                Spawn* spawn = pc->spawn;
                if (spawn && (spawn->type == 0 || spawn->owner_38c != 0 || spawn->flag_208 != 0))
                    extra += static_cast<std::uint32_t>(q.cached_item_effect(pc, 0x110));
                level = static_cast<std::int32_t>(static_cast<std::uint32_t>(level) + extra);
            }
        }
    }
    // 0x43d3ff
    if (caster->type == 0 && is_stack_blocked(self, spell, caster->class_id, q)) return nullptr;
    dot = is_stackable_dot(*spell) ? 1 : 0;
    found = 0;
    limit = max_effects(self);
    // 0x43d474: the same spell already there?
    for (i = 0; i < limit; ++i) {
        effect = get_effect(i, q);
        if (effect->type != 0 && effect->spell_id == spell_id) {
            bool clear;
            if (!self->spawn) clear = true;
            else if (affects(spell, 0x1a8)) clear = spell_id == 0xac3;
            else clear = caster->type != 0 || self->spawn->type != 1;
            if (clear) dot = 0;
            if (dot == 0 || caster->spawn_id == q.buff_caster_id(i)) goto same_spell; // 0x43d572
            found = 1;
        }
    }
    // 0x43d536
    if (dot != 0 && found != 0 && limit > 0) {
        std::int32_t empty = -1;
        for (i = 0; i < limit; ++i) {
            effect = get_effect(i, q);
            if (effect->type == 0) {
                if (empty == -1) empty = i;
                continue;
            }
            if (spell_id != effect->spell_id || !self->spawn) continue;
            Spawn* spawn = q.spawn_by_id(q.buff_caster_id(i));
            if (!spawn || spawn == self->spawn) {
                *slot = i;
                return get_effect(i, q);
            }
        }
        if (empty != -1) {
            *slot = empty;
            return get_effect(empty, q);
        }
    }
    // 0x43d6b1: compare with every existing buff
    index = 0;
    if (limit <= 0) goto find_open;
    do {
        i = static_cast<std::int32_t>(index);
        current = get_effect(i, q);
        if (current->type == 0) goto next_buff;
        existing_id = current->spell_id;
        if (!valid_spell(existing_id) || !(existing = q.spell_by_id(existing_id))) {
            current->modifier = 0x3f800000u; // 1.0f
            current->spell_id = -1;
            current->type = 0;
            current->level = 0;
            current->duration = 0;
            current->byte_2 = 0;
            current->slot_data.fill(0); // EQ_Affect::FillSlotData(0)
            goto next_buff;
        }
        if (caster->class_id == 8 && spell_level_needed(*existing, 8) == 0xff &&
            bard_cast_bard(spell, caster->class_id) != 0)
            goto next_buff;
        if (spell->spell_type != 0 && existing->spell_type != 0 && spell_level_needed(*existing, 8) != 0 &&
            spell_level_needed(*existing, 8) < 0xff && spell_level_needed(*spell, 8) == 0xff)
            goto next_buff;
        for (j = 0;;) {
            if (spa_at(existing, j) == 0xfe || spa_at(spell, j) == 0xfe) goto next_buff;
            if (spa_at(spell, j) == 0x2d) goto blocked;
            {
                std::int32_t spa = spa_at(existing, j);
                if ((spa == 0xf || spa == 0x1a1 || spa == 0 || spa == 0x64 || spa == 0x14e) &&
                    spell_level_needed(*existing, 8) != 0xff && spell_level_needed(*spell, 8) == 0xff)
                    goto next_slot;
            }
            if (spa_at(existing, j) != spa_at(spell, j)) goto next_slot;
            if (is_spa_stacking(spa_at(existing, j)) && base_at(existing, j) > base_at(spell, j)) goto blocked;
            if (is_spa_ignored_by_stacking(spa_at(spell, j))) goto next_slot;
            {
                std::int32_t spa = spa_at(spell, j);
                if (spa > 0x1a0) {
                    switch (static_cast<std::uint32_t>(spa - 0x1bc)) {
                    case 0: *slot = static_cast<std::int32_t>(j); goto next_slot;
                    case 6: goto larger_max;
                    case 7:
                    case 8: goto smaller_max;
                    default: goto rules;
                    }
                }
                if (spa == 0x1a0) goto non_negative;
                if (spa > 0xa2) {
                    if (spa == 0x14e) goto non_negative;
                    goto rules;
                }
                if (spa >= 0xa1) goto larger_max;
                if (spa < 0) goto rules;
                if (spa <= 1) goto non_negative;
                if (spa != 0xa) goto rules;
                if (base_at(spell, j) == 0) goto next_slot;
                if (base_at(existing, j) != 0) goto rules;
                goto next_slot;
            non_negative:
                if (base_at(spell, j) < 0) goto next_slot;
                goto rules;
            larger_max:
                if (base_at(spell, j) < base_at(existing, j)) goto blocked;
                if (base_at(spell, j) != base_at(existing, j)) goto rules;
                if (spell->max[j] >= slot_data_value(current, j)) goto rules;
                goto blocked;
            smaller_max:
                if (base_at(spell, j) > base_at(existing, j)) goto blocked;
                if (base_at(spell, j) != base_at(existing, j)) goto rules;
                if (spell->max[j] < slot_data_value(current, j)) goto blocked;
                goto rules;
            }
        rules: // 0x43da66
            if (spell->spell_type != 0) {
                if (is_click_off_allowed(*existing)) {
                    if (spa_at(spell, j) != 3) goto blocked;
                    goto next_slot;
                }
                if (affects(existing, 0x3a) || affects(existing, 0x9c)) goto blocked;
            }
            if (spa_at(existing, j) == 0x65) goto blocked;
            if ((existing_id >= 0x307 && existing_id <= 0x311) || (existing_id >= 0x4b0 && existing_id <= 0x4e2) ||
                (existing_id >= 0x76c && existing_id <= 0x784) || existing_id == 0x81f || existing_id == 0xabf ||
                is_res_effect_spell(existing_id) || existing_id == 0x344)
                goto blocked;
            old_change = q.calc_affect_change(existing, current->level, j, static_cast<std::int32_t>(index));
            new_change = q.calc_affect_change(spell, static_cast<std::uint32_t>(level), j, static_cast<std::int32_t>(index));
            if (spell_id == 0x654 || spell_id == 0x718 || spell_id == 0x341 || existing_id == 0x716) new_change = -1;
            if (existing_id == 0x654 || existing_id == 0x718 || existing_id == 0x341 || existing_id == 0x716)
                old_change = -1;
            {
                bool overwrite; // 0x43dbf1
                if (old_change < 0) overwrite = new_change > 0;
                else if (old_change == 0) overwrite = false;
                else overwrite = new_change < 0;
                if (overwrite) {
                    if (spa_at(spell, j) != 3) goto take_slot;
                    goto next_slot;
                }
            }
            {
                bool resurrect = spell_id == 0x344; // 0x43dc20
                bool keep_old;
                if (spa_at(spell, j) == 0xb) {
                    if (new_change < 0x64 && new_change <= old_change) keep_old = false;
                    else if (old_change <= 0x64) keep_old = true;
                    else keep_old = new_change >= 0x64 && new_change < old_change;
                } else if (old_change < 0) {
                    keep_old = !(new_change <= old_change);
                } else if (old_change == 0) {
                    keep_old = true;
                } else {
                    keep_old = new_change < old_change;
                }
                if (keep_old && !resurrect) goto blocked;
                if (spell->spell_type == 0 && existing->spell_type == 0) goto replace_detrimental;
                std::int32_t current_slot = *slot;
                if (current_slot != -1 && get_effect(current_slot, q)->type != 0) goto next_buff;
                *slot = static_cast<std::int32_t>(index);
                goto next_buff;
            }
        next_slot: // 0x43dc09
            j = static_cast<std::uint8_t>(j + 1);
            if (j < kSpellSlots) continue;
            goto next_buff;
        }
    next_buff: // 0x43dcb7
        ++index;
    } while (static_cast<std::int32_t>(index) < limit);
    goto find_open;

take_slot: // 0x43dd82
    if (spa_at(spell, j) == 0 && old_change < 0 && new_change > 0) goto blocked;
replace_detrimental: // 0x43de05 (the same code as take_slot's tail)
    i = static_cast<std::int32_t>(index);
    *slot = i;
    if (static_cast<std::uint8_t>(keep) == 0 && spell_id != current->spell_id) q.remove_affect(get_effect(i, q));
    result = open_effect_slot(self, spell->short_buff, spell->is_skill, i, q);
    *slot = result;
    return get_effect(result, q);

find_open: // 0x43dcce
    short_only = spell->short_buff;
    song = spell->spell_type != 0 ? spell->is_skill : 0;
    result = open_effect_slot(self, short_only, song, -1, q);
    if (*slot == -1) {
        *slot = result;
        goto use_slot;
    }
    if (result != -1) goto use_slot;
    {
        std::int32_t prior = *slot; // read before the checks below, as the original does
        std::int32_t floor = 0;     // 0x43de82
        Spawn* spawn = self->spawn;
        if (spawn && (spawn->type == 0 || spawn->flag_208 != 0)) {
            if (short_only != 0) floor = max_effects(self) - 0x15;
            else if (song != 0) floor = max_effects(self) - 1;
        }
        if (prior >= floor && *slot <= last_effect_slot(self, short_only, song, 0, q)) goto use_slot;
        *slot = -1;
        goto drop_one;
    }
use_slot: // 0x43dd18
    if (*slot == -1) goto drop_one;
    if (static_cast<std::uint8_t>(keep) == 0 && get_effect(*slot, q)->type != 0 &&
        spell_id != get_effect(*slot, q)->spell_id)
        q.remove_affect(get_effect(*slot, q));
    result = open_effect_slot(self, short_only, song, static_cast<std::int32_t>(index), q);
    *slot = result;
    return get_effect(result, q);

drop_one: // 0x43dee4
    if (spell->spell_type != 0 || !self->spawn) return nullptr;
    if (q.spawn_vf_dc(self->spawn)) return nullptr;
    for (std::int32_t k = 0; k < limit; ++k) {
        std::int32_t id = get_effect(k, q)->spell_id;
        if (!valid_spell(id)) continue;
        const Spell* other = q.spell_by_id(id);
        if (!other || other->no_dispell != 0 || other->spell_type < 1 || other->no_remove != 0) continue;
        if (static_cast<std::uint8_t>(keep) == 0) q.remove_affect(get_effect(k, q));
        *slot = k;
        result = open_effect_slot(self, short_only, song, k, q);
        *slot = result;
        return get_effect(result, q);
    }
    return nullptr;

same_spell: // 0x43d572: an existing copy of the spell at buff i
    if (level < effect->level) {
        if (affects(spell, 0x1bc)) {
            *slot = i;
            return get_effect(i, q);
        }
        *slot = -1;
        return nullptr;
    }
    if (affects(spell, 0x43) || affects(spell, 0x65) || affects(spell, 0x71)) {
        *slot = -1;
        return nullptr;
    }
    *slot = i;
    return get_effect(i, q);

blocked: // 0x43da03 / 0x43d5ff / 0x43d5bf
    *slot = -1;
    return nullptr;
}

} // namespace hailstone::game::mechanics::affect
