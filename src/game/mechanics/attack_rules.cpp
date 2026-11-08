#include "game/mechanics/attack_rules.h"

namespace hailstone::game::mechanics {

namespace {

// Deity alignment groups of deities 201..216 (the byte table at 0x59e9dc).
constexpr std::uint8_t kDeityGroups[16] = {0, 1, 0, 2, 1, 0, 1, 2, 1, 2, 0, 2, 1, 1, 2, 1};
constexpr std::int32_t kSpellUnaffected = 0x6eb; // a spell exempt from the body-environment rule
constexpr std::int32_t kSpell0x1a8 = 0x1a8;      // counted as a group-only spell

// The spawn's character is level capped (only players: type 0).
const AttackCharacter* capped_character(const AttackSpawn& s) {
    if (s.type != 0 || !s.character) return nullptr;
    return s.character->level_capped ? s.character : nullptr;
}

// A pet NPC's top master, when that is a player.
const AttackSpawn* player_master(const AttackSpawn& s, AttackQueries& q) {
    if (s.type != 1 || s.master_id == 0) return nullptr;
    const AttackSpawn* m = q.spawn_by_id(q.top_master_id(s));
    return m && m->type == 0 ? m : nullptr;
}

} // namespace

bool not_on_same_deity(const AttackSpawn* self, const AttackSpawn* other, bool spell, bool other_is_gm) {
    std::uint32_t mine = 0;
    if (self) {
        if (other && other_is_gm && !spell) return false;
        mine = self->deity;
    }
    std::uint32_t theirs = other ? other->deity : 0;
    std::uint32_t index = mine - 0xc9;
    if (index > 0xf) return false;
    std::uint32_t other_index = theirs - 0xc9;
    return !(other_index <= 0xf && kDeityGroups[other_index] == kDeityGroups[index]);
}

bool allowed_to_attack(const AttackSpawn& self, const AttackSpawn* target_arg, std::int32_t spell,
                       const AttackRules& rules, AttackQueries& q) {
    const AttackSpawn* attacker = q.attacker_of(self);
    if (!target_arg) return false;
    const AttackSpawn* target = target_arg;
    std::uint32_t attacker_id = attacker->id;
    if (target->master_id == attacker_id || q.top_master_id(*target) == attacker_id) {
        // Spells on one's own pet must be beneficial.
        if (!q.is_valid_spell_id(spell)) return false;
        const AttackSpell* s = q.spell_by_id(spell);
        if (!s || s->beneficial < 1) return false;
    }
    std::int32_t attacker_level = q.level(*attacker);
    std::int32_t target_level = q.level(*target);
    if (capped_character(self)) {
        if (self.level_cap <= 0) return true;
        return !(self.level_cap < q.level(*target));
    }
    if (const AttackCharacter* c = capped_character(*target)) {
        if (c->spawn->level_cap <= 0) return true;
        std::int32_t level = q.level(self);
        return !(c->spawn->level_cap < level);
    }
    std::int32_t a = q.level(*attacker);
    std::int32_t t = q.level(*target);
    std::int32_t diff = a - t < 0 ? t - a : a - t;
    if (q.is_vehicle_race(*target) || q.is_vehicle_race(*attacker)) return false;
    if (attacker->type != 1 && attacker->body_wet != target->body_wet && spell != kSpellUnaffected &&
        spell != kMeleeAttack) {
        if (self.type != 0 || target->type != 0) return false;
    }
    if (attacker->in_pvp_area && target->in_pvp_area) {
        if (!q.is_valid_spell_id(spell)) return true;
        const AttackSpell* s = q.spell_by_id(spell);
        if (s && !q.spell_affects(*s, 0x19)) return true;
    }
    if (target == attacker && q.is_valid_spell_id(spell)) return true;
    if (const AttackSpawn* m = player_master(*target, q)) {
        target = m;
        target_level = q.level(*m);
    }
    if (const AttackSpawn* m = player_master(*attacker, q)) {
        attacker = m;
        attacker_level = q.level(*m);
    }
    std::uint8_t attacker_type = attacker->type;
    std::uint8_t target_type = target->type;
    const AttackSpell* s = nullptr;
    if (q.is_valid_spell_id(spell)) {
        s = q.spell_by_id(spell);
        if (!s) return false;
    }
    if (target_type == 1 || attacker_type == 1) return true;
    if (rules.dueling) {
        if (s && s->beneficial && target->pvp != attacker->pvp) return false;
        if (rules.duel_target_id == target->id || rules.duel_target_id == attacker->id) return true;
    }
    if (spell == kMeleeAttack && target->type == 0 && attacker->type == 0 && attacker->guild_id != -1 &&
        target->guild_id != -1)
        return true;
    // The rule set's level limits: both at least level 6 and, unless rule set 6, within 4 levels; rule set 2:
    // within 8 levels; below level 6 only outside newbie zones (after the deity check of rule set 4).
    auto level6 = [&] { return attacker_level >= 6 && target_level >= 6; };
    auto pvp_range = [&](std::int32_t r) {
        if (diff > 4 && r != 6) return false;
        return level6();
    };
    auto newbie_zone = [&] {
        if (level6()) return true;
        return !q.is_newbie_zone(target->zone);
    };
    auto rules2 = [&] { return diff <= 8 && level6(); };
    if (s && s->beneficial == 0) {
        std::int32_t r = rules.pvp_rules;
        if (r == 2) return rules2();
        if (r == 4) {
            if (q.not_on_same_deity(*target, *attacker, s)) return newbie_zone();
        } else if (r == 6) {
            return newbie_zone();
        }
        if (!target->pvp || !attacker->pvp) return false;
        return pvp_range(r);
    }
    if (s) {
        bool owner = q.has_owner(*target);
        if (q.is_gm(*attacker)) return true;
        std::uint8_t beneficial = s->beneficial;
        if (beneficial > 1) {
            if (q.is_corpse(*target) || target->type == 1 || attacker->type != 0) return false;
            if (!q.spell_affects(*s, 0x51)) {
                if (!owner) return false;
                if (!q.is_self_or_group_member(*attacker)) return false;
            }
        } else if (beneficial == 1) {
            std::int32_t tt = s->target_type;
            bool group_only = tt == 0xc || tt == 0x1c || tt == 0x1d || tt == 0x13a || tt == 0x13b || tt == 0x13c ||
                              spell == kSpell0x1a8;
            if (group_only && target->type == 0) {
                if (!owner || !q.is_self_or_group_member(*attacker)) return false;
            }
        }
        if (target->pvp != attacker->pvp || rules.dueling) return false;
        if (rules.pvp_rules != 4 || q.spell_affects(*s, 0x1b)) return true;
        return !q.not_on_same_deity(*target, *attacker, s);
    }
    std::int32_t r = rules.pvp_rules;
    if (r == 2) return rules2();
    if (r == 4) {
        if (q.not_on_same_deity(*target, *attacker, nullptr)) return newbie_zone();
    }
    if (!target->pvp || !attacker->pvp) return false;
    return pvp_range(r);
}

} // namespace hailstone::game::mechanics
