#include "game/mechanics/item_sums.h"

#include "game/mechanics/item_stats.h"
#include "net/item_packets.h"

namespace hailstone::game::mechanics {

namespace {

std::int32_t add(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

// process_all_stats.inc: where each row is summed (a CharacterBase stat
// behind the virtual base, or a CharacterZoneClient field) and, for the
// damage bonuses, the skill whose slot (FUN_004437d0) indexes +0x4c.
struct StatRow {
    bool via_base;
    std::uint32_t offset;
    std::int32_t damage_skill;
};
constexpr std::array<StatRow, kItemStatCount> kRows = {{
    {true, 0x4a4, 0},  {true, 0x4a8, 0},  {true, 0x4ac, 0},  {true, 0x4b0, 0},  {true, 0x4b4, 0},
    {true, 0x4b8, 0},  {true, 0x4bc, 0},  {true, 0x4c4, 0},  {true, 0x4d4, 0},  {true, 0x4d0, 0},
    {true, 0x4c0, 0},  {true, 0x4c8, 0},  {true, 0x4cc, 0},  {false, 0x24, 0},  {false, 0x28, 0},
    {false, 0x2c, 0},  {false, 0x30, 0},  {false, 0x34, 0},  {false, 0x38, 0},  {false, 0x3c, 0},
    {false, 0x40, 0},  {false, 0x44, 0},  {false, 0x98, 0},  {false, 0x9c, 0},  {false, 0xc8, 0},
    {false, 0xcc, 0},  {false, 0xd0, 0},  {false, 0xd4, 0},  {false, 0xd8, 0},  {false, 0xdc, 0},
    {false, 0xe0, 0},  {false, 0xe4, 0},  {false, 0xe8, 0},  {false, 0xec, 0},  {false, 0xf0, 0},
    {false, 0xf4, 0},  {false, 0xf8, 0},  {false, 0xfc, 0},  {false, 0x100, 0}, {false, 0x10c, 0},
    {false, 0x48, 0},  {false, 0x110, 0}, {false, 0x114, 0}, {false, 0x118, 0}, {false, 0x11c, 0},
    {false, 0x120, 0}, {false, 0x128, 0}, {false, 0x4c, 10}, {false, 0x4c, 8},  {false, 0x4c, 21},
    {false, 0x4c, 23}, {false, 0x4c, 26}, {false, 0x4c, 30}, {false, 0x4c, 38}, {false, 0x4c, 52},
    {false, 0x4c, 74},
}};

// AddItemStats: every row added in order, through the scaler for a worn item
// that has a recommended level or skill.
void add_item_stats(ItemSums& sums, const StatItem& item, ItemSumQueries& q, bool scaled, std::uint32_t skill) {
    for (int k = 0; k < kItemStatCount; ++k) {
        const StatRow& row = kRows[static_cast<std::size_t>(k)];
        std::uint32_t slot = 0;
        if (row.damage_skill != 0) slot = q.damage_slot(row.damage_skill); // asked before the value (0..8 asserted)
        std::int32_t value = item.stats[static_cast<std::size_t>(k)];
        if (scaled) value = q.recommended_scale(item, value, skill);
        std::uint32_t at = row.damage_skill != 0 ? row.offset + slot * 4 : row.offset;
        std::int32_t& field = row.via_base ? sums.base_at(at) : sums.at(at);
        field = add(field, value);
    }
}

// The race bit of a race: races below 13 are their own index, then 0x80 13,
// 0x82 14, 0x14a 15, 0x20a 16, anything else 17; the bit is index - 1.
std::uint32_t race_bit(std::int32_t race) {
    std::int32_t index = race;
    if (race >= 0xd) {
        if (race > 0x14a) index = race == 0x20a ? 0x10 : 0x11;
        else if (race == 0x14a) index = 0xf;
        else if (race == 0x80) index = 0xd;
        else if (race == 0x82) index = 0xe;
        else index = 0x11;
    }
    return 1u << ((static_cast<std::uint32_t>(index) - 1) & 31);
}

// WornItemCounts: the level, then unless any item is allowed the race, the
// deity (with a PcBase and a deity mask) and the prestige limit.
bool worn_item_counts(const StatItem& item, ItemSumQueries& q) {
    if (item.required_level > q.level()) return false;
    if (q.any_race_deity()) return true;
    if ((race_bit(q.race()) & item.races) == 0) return false;
    if (q.has_pc_base() && item.deities != 0) {
        std::int32_t deity = q.deity();
        std::uint32_t index = deity == 0x18c ? 1 : static_cast<std::uint32_t>(deity - 0xc7);
        if (((1u << ((index - 1) & 31)) & item.deities) == 0) return false;
    }
    std::int32_t limit = q.limit(0x13);
    return limit == -1 || item.prestige <= limit;
}

void cap_at(ItemSums& sums, std::uint32_t offset, std::int32_t cap) {
    if (sums.at(offset) > cap) sums.at(offset) = cap;
}

// KeepWornSums: the worn stats copied to +0x510.., the worn bonuses to
// +0x12c..+0x198, and those copies capped.
void keep_worn_sums(ItemSums& sums) {
    for (std::uint32_t at = 0x4a4; at <= 0x4d8; at += 4) sums.base_at(at + 0x6c) = sums.base_at(at);
    constexpr std::uint32_t kFrom[] = {0x24, 0x28, 0x2c, 0x30, 0x34, 0x38, 0x3c, 0x40, 0x44, 0x98,
                                       0x48, 0x110, 0x114, 0x118, 0x11c, 0x120, 0x9c, 0x124, 0x128};
    for (std::uint32_t k = 0; k < std::size(kFrom); ++k) sums.at(0x12c + k * 4) = sums.at(kFrom[k]);
    for (std::uint32_t k = 0; k < 9; ++k) sums.at(0x178 + k * 4) = sums.at(0x4c + k * 4);
    if (sums.at(0x12c) < 0) sums.at(0x12c) = 0;
    for (std::uint32_t at : {0x13cu, 0x140u, 0x14cu, 0x154u, 0x150u, 0x168u}) cap_at(sums, at, 35);
    cap_at(sums, 0x16c, 25);
    cap_at(sums, 0x144, 100);
    cap_at(sums, 0x148, 150);
    cap_at(sums, 0x138, 100);
    cap_at(sums, 0x178, 100);
    cap_at(sums, 0x17c, 125);
    for (std::uint32_t at = 0x180; at <= 0x194; at += 4) cap_at(sums, at, 100);
    cap_at(sums, 0x198, 125);
}

} // namespace

std::uint32_t damage_bonus_slot(std::int32_t skill) {
    for (std::uint32_t k = 0; k < kDamageBonusSkills.size(); ++k)
        if (kDamageBonusSkills[k] == skill) return k;
    return 0xffffffffu;
}

StatItem stat_item(const net::Item& item) {
    StatItem s;
    s.source = &item;
    s.kind = item.item_class;
    s.required_level = item_required_level(item);
    s.races = item_races(item);
    s.deities = item_deity(item);
    s.prestige = static_cast<std::int32_t>(item.prestige); // GetPrestige(0): the item's own
    s.charm_file_id = static_cast<std::int32_t>(item.charm_file_id);
    s.recommended_skill = item_recommended_skill(item);
    s.recommended_level = item_recommended_level(item);
    s.item_type = item.item_type;
    using F = std::int32_t (*)(const net::Item&);
    // The getters sum the item and its augments (item_stat); the potion belt
    // slots are GetNumPotionSlots(1), the damage bonuses GetDmgBonusValue(skill, 1).
    static constexpr F kFields[46] = {
        [](const net::Item& i) { return static_cast<std::int32_t>(i.astr); },
        [](const net::Item& i) { return static_cast<std::int32_t>(i.asta); },
        [](const net::Item& i) { return static_cast<std::int32_t>(i.acha); },
        [](const net::Item& i) { return static_cast<std::int32_t>(i.adex); },
        [](const net::Item& i) { return static_cast<std::int32_t>(i.aint); },
        [](const net::Item& i) { return static_cast<std::int32_t>(i.aagi); },
        [](const net::Item& i) { return static_cast<std::int32_t>(i.awis); },
        [](const net::Item& i) { return static_cast<std::int32_t>(i.mr); },
        [](const net::Item& i) { return static_cast<std::int32_t>(i.cr); },
        [](const net::Item& i) { return static_cast<std::int32_t>(i.fr); },
        [](const net::Item& i) { return static_cast<std::int32_t>(i.pr); },
        [](const net::Item& i) { return static_cast<std::int32_t>(i.dr); },
        [](const net::Item& i) { return static_cast<std::int32_t>(i.sv_corruption); },
        [](const net::Item& i) { return i.hp; },
        [](const net::Item& i) { return i.mana; },
        [](const net::Item& i) { return static_cast<std::int32_t>(i.endurance); },
        [](const net::Item& i) { return i.combat_effects; },
        [](const net::Item& i) { return i.shielding; },
        [](const net::Item& i) { return i.spell_shield; },
        [](const net::Item& i) { return i.avoidance; },
        [](const net::Item& i) { return i.accuracy; },
        [](const net::Item& i) { return i.stun_resist; },
        [](const net::Item& i) { return i.dot_shield; },
        [](const net::Item& i) { return static_cast<std::int32_t>(i.ds_mitigation); },
        [](const net::Item& i) { return i.heroic_str; },
        [](const net::Item& i) { return i.heroic_int; },
        [](const net::Item& i) { return i.heroic_wis; },
        [](const net::Item& i) { return i.heroic_agi; },
        [](const net::Item& i) { return i.heroic_dex; },
        [](const net::Item& i) { return i.heroic_sta; },
        [](const net::Item& i) { return i.heroic_cha; },
        [](const net::Item& i) { return i.heroic_mr; },
        [](const net::Item& i) { return i.heroic_fr; },
        [](const net::Item& i) { return i.heroic_cr; },
        [](const net::Item& i) { return i.heroic_dr; },
        [](const net::Item& i) { return i.heroic_pr; },
        [](const net::Item& i) { return i.heroic_sv_corrup; },
        [](const net::Item& i) { return i.heal_amt; },
        [](const net::Item& i) { return i.spell_dmg; },
        [](const net::Item& i) { return i.clairvoyance; },
        [](const net::Item& i) { return i.strike_through; },
        [](const net::Item& i) { return i.attack; },
        [](const net::Item& i) { return i.regen; },
        [](const net::Item& i) { return i.mana_regen; },
        [](const net::Item& i) { return i.end_regen; },
        [](const net::Item& i) { return i.damage_shield; },
    };
    for (std::size_t k = 0; k < std::size(kFields); ++k) s.stats[k] = item_stat(item, kFields[k]);
    s.stats[std::size(kFields)] = item_potion_slots(item, true);
    for (std::size_t k = 0; k < kDamageBonusSkills.size(); ++k)
        s.stats[std::size(kFields) + 1 + k] = item_dmg_bonus(item, kDamageBonusSkills[k], true);
    return s;
}

void sum_item_stats(ItemSums& sums, const ItemSources& sources, ItemSumQueries& q) {
    // ClearSums: everything but the endurance upkeep (+0x94).
    sums.base.fill(0);
    for (std::uint32_t at = 0x24; at < 0x19c; at += 4)
        if (at != 0x94) sums.at(at) = 0;
    for (const StatItem* item : sources.worn) {
        if (!item || item->kind != 0 || !worn_item_counts(*item, q)) continue;
        bool plain = item->charm_file_id != 0 || (item->recommended_skill == 0 && item->recommended_level == 0);
        std::uint8_t skill = q.item_type_to_skill(item->item_type);
        add_item_stats(sums, *item, q, !plain, skill);
    }
    keep_worn_sums(sums);
    for (auto source : {sources.source_b, sources.source_c, sources.own_4c, sources.own_54})
        for (const StatItem* item : source)
            if (item && item->kind == 0) add_item_stats(sums, *item, q, false, 0);
}

void derive_item_bonuses(ItemSums& sums, const std::function<std::int32_t(std::int32_t value, int kind)>& adjust) {
    if (sums.at(0x24) < 0) sums.at(0x24) = 0;
    constexpr std::uint32_t kCopies[][2] = {{0xa8, 0x34}, {0xc0, 0x44}, {0xa4, 0x38}, {0xc4, 0x48}, {0xb4, 0x9c},
                                            {0xb0, 0x98}, {0xac, 0x120}, {0xbc, 0x40}, {0xb8, 0x3c}, {0xa0, 0x30}};
    for (const auto& c : kCopies) sums.at(c[0]) = sums.at(c[1]);
    for (std::uint32_t k = 0; k < 9; ++k) sums.at(0x70 + k * 4) = sums.at(0x4c + k * 4);
    constexpr std::uint32_t kAdjusted[][2] = {{0x34, 6},   {0x38, 4},   {0x44, 0xc},  {0x48, 0xd},  {0x98, 8},
                                              {0x9c, 9},   {0x120, 7},  {0x3c, 0xa},  {0x40, 0xb},  {0x30, 5},
                                              {0x4c, 0x11}, {0x50, 0x12}, {0x54, 0x13}, {0x58, 0x14}, {0x5c, 0x15},
                                              {0x60, 0x16}, {0x64, 0x17}, {0x68, 0x18}, {0x6c, 0x19}};
    for (const auto& a : kAdjusted) sums.at(a[0]) = adjust(sums.at(a[0]), static_cast<int>(a[1]));
}

} // namespace hailstone::game::mechanics
