#include "game/mechanics/combat_ability.h"

#include <array>
#include <cctype>

namespace hailstone::game::mechanics::combat_ability {

namespace {

constexpr std::uint32_t kDeityAgnostic = 396;
constexpr std::uint32_t kFirstDeity = 200;
constexpr std::int32_t kSelf = 6; // target type 6: self

// __strnicmp(name, args, n) == 0 for a name shorter than n only when it ends where the argument does.
bool name_starts_with(std::string_view name, std::string_view prefix) {
    if (name.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(name[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    return true;
}

// The time left as dsp_chat_EqStrTemplate gets it: 5807 (name, hours, minutes, seconds) or 5808 (name, minutes,
// seconds); returns the seconds.
std::uint32_t say_time_left(const Spell& s, std::uint32_t left, Queries& q) {
    const std::uint32_t hours = left / 3600;
    left -= hours * 3600;
    const std::uint32_t minutes = left / 60;
    const std::uint32_t seconds = left - minutes * 60;
    if (hours != 0)
        q.eqstr_template(kEqstrReadyHours, kColor,
                         {s.name, std::to_string(hours), std::to_string(minutes), std::to_string(seconds)});
    else
        q.eqstr_template(kEqstrReadyMinutes, kColor, {s.name, std::to_string(minutes), std::to_string(seconds)});
    return seconds;
}

} // namespace

std::uint32_t deity_bitmask(std::uint32_t deity) {
    if (deity == kDeityAgnostic) return 1;
    return 1u << ((deity - kFirstDeity) & 31);
}

bool has_combat_ability(std::int32_t spell_id, Queries& q) {
    const Spell* spell = q.spell_by_id(spell_id);
    for (int i = 0; i < kCombatAbilities; ++i) {
        const std::int32_t ability = q.combat_ability(i);
        if (ability == spell_id) return true;
        if (spell && spell->group > 0) {
            const Spell* other = q.spell_by_id(ability);
            if (other && other->group == spell->group && other->subgroup == spell->subgroup &&
                other->rank >= spell->rank)
                return true;
        }
    }
    return false;
}

int can_use_melee_combat_ability(std::int32_t spell_id, Queries& q) {
    const Spell* spell = q.spell_by_id(spell_id);
    if (!spell || spell->is_skill == 0) return 0;
    const std::uint8_t needed = q.spell_level_needed(*spell, q.class_id());
    if (needed == 0xff) return -1;
    if (needed > q.level()) return -2;
    if (spell->deity_mask != 0 && spell->deity_mask != deity_bitmask(q.deity())) return -3;
    const std::int32_t limit = q.character_virtual_90() ? -1 : q.character_rank(3);
    if (spell->group > 0) {
        for (int i = 0; i < kCombatAbilities; ++i) {
            const Spell* other = q.spell_by_id(q.combat_ability(i));
            if (other && other->group == spell->group && other->subgroup == spell->subgroup &&
                other->rank > spell->rank && (limit == -1 || limit >= other->rank))
                return -4;
        }
    }
    if (limit != -1 && limit < spell->rank) return -5;
    return 1;
}

bool is_combat_ability_class(std::uint32_t class_id) {
    static constexpr std::array<std::uint32_t, 9> kClasses = {1, 7, 9, 3, 4, 5, 8, 15, 16};
    for (std::uint32_t c : kClasses)
        if (c == class_id) return true;
    return false;
}

bool do_combat_ability(std::int32_t spell_id, bool rank, Queries& q) {
    const Spell* spell = q.spell_by_id(spell_id);
    if (!spell) return false;
    if (!q.take_alt_attack()) {
        if (rank && !q.character_virtual_90()) {
            const std::int32_t limit = q.character_rank(3);
            if (limit != -1 && limit < spell->rank) {
                spell = q.spell_by_group_and_rank(spell->group, spell->subgroup, limit, 1);
                if (!spell) return false;
                spell_id = spell->id;
            }
        }
        if (!has_combat_ability(spell_id, q)) return false;
        if (can_use_melee_combat_ability(spell_id, q) != 1) return false;
    }
    // GetCombatAbilityTimer for ids 0..19 (an unsigned compare), else ready.
    const auto timer = static_cast<std::uint32_t>(spell->timer);
    const std::uint32_t ready = timer < static_cast<std::uint32_t>(kTimers) ? q.combat_ability_timer(spell->timer) : 0;
    if (ready > q.fast_time()) {
        const std::uint32_t again = timer < static_cast<std::uint32_t>(kTimers) ? q.combat_ability_timer(spell->timer) : 0;
        const std::uint32_t seconds = say_time_left(*spell, again - q.fast_time(), q);
        // The original goes on when the seconds part alone exceeds the recast time in seconds.
        if (seconds <= spell->recast_time / 1000) return false;
    }
    if (spell->duration_formula != 0 && spell->target_type == kSelf && q.discipline_active()) {
        q.message(kEqstrAlreadyActive, kColor);
        return false;
    }
    q.cast_spell(spell_id);
    return true;
}

void Queries::use_combat_ability(std::int32_t spell_id) { do_combat_ability(spell_id, true, *this); }

void discipline_command(std::string_view args, Queries& q) {
    if (!is_combat_ability_class(q.class_id())) {
        q.dsp_chat_2(kEqstrWrongClass);
        return;
    }
    for (int i = 0; i < kCombatAbilities; ++i) {
        if (q.combat_ability(i) == 0) continue;
        const Spell* spell = q.spell_by_id(q.combat_ability(i));
        if (!spell) continue;
        if (!args.empty()) {
            if (name_starts_with(spell->name, args)) {
                q.use_combat_ability(spell->id);
                return;
            }
            continue;
        }
        // The timer, then the time, as written: the order of an operator's operands is the compiler's.
        if (const auto timer = q.discipline_reuse_timer(spell->timer); timer <= q.fast_time()) continue;
        const auto timer = q.discipline_reuse_timer(spell->timer);
        say_time_left(*spell, timer - q.fast_time(), q);
    }
}

} // namespace hailstone::game::mechanics::combat_ability
