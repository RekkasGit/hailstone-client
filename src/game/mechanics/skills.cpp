// Skill values and caps: GetRacialSkillBonus @0x444bd0, CSkillMgr::GetSkillCap @0x5baec0,
// CSkillMgr_GetSkillCapWithEffects @0x5ba600, GetAdjustedSkill @0x449fe0 and
// CharacterZoneClient_GetSkillWithItemMods @0x5826e0, checked against the original's vectors.
#include "game/mechanics/character.h"

#include "core/wrap_math.h"
#include "game/mechanics/item_stats.h"
#include "game/spells.h"

namespace hailstone::game::mechanics {

namespace {

constexpr std::int32_t kRacialSkill = 0x1b;  // its cap takes the racial bonus and SPA 0x174
constexpr std::int32_t kGnomeSkill = 0x39;   // tinkering: 0 unless the race (+0x3370) is 12
constexpr std::uint32_t kGnome = 12;

std::int32_t add(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

// The first used buff slot whose spell has the SPA before its first blank effect (GetPCSpellAffect @0x433fd0).
const SheetBuff* pc_spell_affect(const Character& ch, std::int32_t spa) {
    const auto& buffs = ch.sheet().buffs;
    for (int i = 0; i < ch.max_effects() && i < static_cast<int>(buffs.size()); ++i) {
        const SheetBuff& b = buffs[static_cast<std::size_t>(i)];
        if (b.type == 0) continue;
        const SpellData* s = ch.spell(b.spell_id);
        if (!s) continue;
        for (std::int32_t e : s->effect) {
            if (e == 254) break;
            if (e == spa) return &b;
        }
    }
    return nullptr;
}

} // namespace

std::int32_t Character::racial_skill_bonus(std::uint32_t skill) const {
    if (hooks.racial_skill_bonus)
        if (auto v = hooks.racial_skill_bonus(skill)) return *v;
    if (skill > 99) return 0;
    if (skill == 75) return 1;
    switch (static_cast<std::int32_t>(sheet_.race)) {
    case 330: return skill == 50 ? 125 : 0;
    case 4: case 112: return skill == 27 || skill == 29 ? 50 : 0;
    case 8: case 94: return skill == 40 ? 50 : 0;
    case 11: case 81: return skill == 42 || skill == 29 ? 50 : 0;
    case 6: case 77: case 98: return skill == 29 ? 50 : 0;
    case 12: return skill == 57 ? 50 : 0;
    case 128: return skill == 27 ? 50 : skill == 50 ? 100 : 0;
    case 130: return skill == 39 || skill == 42 ? 50 : 0;
    default: return 0;
    }
}

namespace {
std::uint32_t cap_cell(const Character& ch, const GameTables& t, std::uint32_t cls, std::uint32_t skill,
                       std::uint32_t level) {
    if (ch.hooks.skill_cap_cell)
        if (auto v = ch.hooks.skill_cap_cell(cls, skill, level)) return static_cast<std::uint32_t>(*v);
    return static_cast<std::uint32_t>(t.skill_caps.cap(cls, skill, level));
}
} // namespace

std::int32_t Character::skill_cap_with_effects(std::uint32_t skill, bool items, bool aa, bool buffs) const {
    auto level = static_cast<std::uint32_t>(max_level());
    if (level == 0x3c) level = 0x41;
    std::uint32_t cap = cap_cell(*this, tables_, sheet_.class_id, skill, level) +
                        static_cast<std::uint32_t>(total_effect(0xf7, items, static_cast<std::int32_t>(skill), aa, buffs));
    if (static_cast<std::int32_t>(skill) == kRacialSkill) {
        if (racial_skill_bonus(kRacialSkill) > static_cast<std::int32_t>(cap))
            cap = static_cast<std::uint32_t>(racial_skill_bonus(kRacialSkill));
        return static_cast<std::int32_t>(cap + static_cast<std::uint32_t>(total_effect(0x174, items, 0, aa, buffs)));
    }
    if (static_cast<std::int32_t>(skill) == kGnomeSkill && sheet_.race != kGnome) return 0;
    return static_cast<std::int32_t>(cap);
}

std::int32_t Character::skill_cap(std::int32_t level, std::int32_t cls, std::int32_t skill, bool with_character,
                                  bool items, bool aa, bool buffs) const {
    if (hooks.skill_cap)
        if (auto v = hooks.skill_cap(level, cls, skill)) return *v;
    auto index = static_cast<std::uint32_t>(skill);
    // The skill's EQ_Skill entry (skills 0..99 have one): LevelCappedSkill (+0x21).
    std::int32_t entry = index <= 0x63 ? (skill_level_capped(index) ? 1 : 0) : -1;
    if (hooks.skill_entry && index <= 0x63)
        if (auto v = hooks.skill_entry(index)) entry = *v;
    if (level > 100) level = 100;
    if (cls > 0x23 || skill >= kSkillCount || entry < 0) return 0;
    bool capped = entry != 0;
    if (!capped && with_character) return skill_cap_with_effects(index, items, aa, buffs);
    std::uint32_t cap = cap_cell(*this, tables_, static_cast<std::uint32_t>(cls), index, static_cast<std::uint32_t>(level));
    if (!capped || !with_character) return static_cast<std::int32_t>(cap);
    cap += static_cast<std::uint32_t>(total_effect(0xf7, items, skill, aa, buffs));
    if (skill == kGnomeSkill) return sheet_.race == kGnome ? static_cast<std::int32_t>(cap) : 0;
    if (skill == kRacialSkill) {
        if (racial_skill_bonus(kRacialSkill) > static_cast<std::int32_t>(cap))
            cap = static_cast<std::uint32_t>(racial_skill_bonus(kRacialSkill));
        return static_cast<std::int32_t>(cap + static_cast<std::uint32_t>(total_effect(0x174, items, 0, aa, buffs)));
    }
    return static_cast<std::int32_t>(cap);
}

std::int32_t Character::adjusted_skill(std::uint32_t skill) const {
    if (hooks.adjusted_skill)
        if (auto v = hooks.adjusted_skill(skill)) return *v;
    return adjusted_skill_as(skill, sheet_.class_id);
}

std::int32_t Character::adjusted_skill_as(std::uint32_t skill, std::uint32_t class_id) const {
    if (skill > 0x63 || (skill == kGnomeSkill && sheet_.race != kGnome)) return 0;
    std::int32_t value = sheet_.skills[skill];
    if (sheet_.has_spawn && sheet_.spawn_type == SpawnType::NPC) return value > 0;
    if (sheet_.profile_null || sheet_.profile_type != 0) return value > 0;
    std::uint32_t cls = class_id;
    std::int32_t rank = sheet_.level;
    if (sheet_.has_spawn && sheet_.spawn_type == SpawnType::PC) {
        std::int32_t highest = static_cast<std::uint8_t>(sheet_.highest_level); // PcBase_GetHighestLevel: a byte
        if (rank < highest) {
            rank += 2;
            if (highest < rank) rank = highest;
        }
    }
    std::int32_t racial = racial_skill_bonus(skill);
    std::int32_t cap = skill_cap(100, static_cast<std::int32_t>(cls), static_cast<std::int32_t>(skill), true);
    auto limit = [&] { // 0x5baa20: EQ_Skill::MinLevel[class]
        if (hooks.skill_min_level)
            if (auto v = hooks.skill_min_level(skill, cls)) return *v;
        return tables_.skill_caps.min_level(skill, cls);
    };
    if (cap == 0 || (value == 0 && limit() > 1)) return racial;
    if (rank < limit()) return value < 1 ? 0 : racial;
    return value < 1 ? 1 : value;
}

std::int32_t Character::skill_with_item_mods(std::uint32_t skill, bool spell_reduction) const {
    if (hooks.skill_with_item_mods)
        if (auto v = hooks.skill_with_item_mods(skill, spell_reduction)) return *v;
    std::int32_t value = skill < kSkills ? sheet_.skills[skill] : 0;
    // The spawn's virtual +0xdc (not modelled: false) would skip the cap.
    if (sheet_.profile_type == 0 && sheet_.has_spawn) {
        std::int32_t level = sheet_.level;
        std::int32_t highest = static_cast<std::uint8_t>(sheet_.highest_level);
        if (level < highest) {
            std::int32_t raised = level + 2;
            level = highest < raised ? highest : raised;
        }
        std::int32_t cap = skill_cap(level, sheet_.class_id, static_cast<std::int32_t>(skill), true);
        std::int32_t raw = skill < kSkills ? sheet_.skills[skill] : 0;
        if (raw < cap) cap = raw;
        std::int32_t racial = racial_skill_bonus(skill);
        value = cap > racial ? cap : racial;
    }
    if (spell_reduction) {
        if (const SheetBuff* b = pc_spell_affect(*this, 0x7a)) {
            const SpellData* s = spell(b->spell_id);
            if (s && static_cast<std::uint32_t>(s->base[0]) == skill) {
                std::int32_t percent = s->formula[0]; // +0xec
                if (percent > 0 && percent < 0x65)
                    value = core::wrapped_mul_div(0x64 - percent, value, 100);
            }
        }
    }
    std::int32_t best = 0;
    for (int slot = 0; slot < kWornSlots; ++slot) {
        const net::Item* item = sheet_.item(slot);
        if (!item || item->item_class != 0) continue;
        std::int32_t mod = item_skill_mod(*item, static_cast<std::int32_t>(skill), value);
        if (mod != 0 && mod > best) best = mod;
    }
    for (const auto& extra : sheet_.extra_items) {
        if (!extra || extra->item_class != 0) continue;
        std::int32_t mod = item_skill_mod(*extra, static_cast<std::int32_t>(skill), value);
        if (mod != 0 && mod > best) best = mod;
    }
    if (best != 0) value = add(value, best);
    std::int32_t result = value;
    if (skill == 0x23) {
        if (const net::Item* held = sheet_.item(0x21)) {
            std::int32_t mod = item_skill_mod(*held, 0x23, value);
            if (held->item_type == 0xc && mod != 0) result = add(result, mod);
        }
    }
    if (result > 0xfc && skill_held_at_252(skill)) return 0xfc;
    return result;
}

} // namespace hailstone::game::mechanics
