#pragma once

// The item part of CharacterZoneClient::ProcessAllStats @0x4547e0: the sums
// of every item stat over the worn items (with the recommended-level scaler)
// and the other item lookups, the worn-only copies and their caps, and the
// copies and adjustments the derived stats start from.
//
// Exact port of rof2decomp batch 242 (re/src/game/character/
// process_all_stats_eh.cpp and its generated stat table
// process_all_stats.inc), checked against the original's vectors
// (tests/mechanics_vectors_test.cpp). The sums live where the original keeps
// them, so they are indexed by the original's field offsets: ItemSums::at()
// takes a CharacterZoneClient offset (+0x24..+0x198), base_at() a
// CharacterBase stat offset behind the virtual base (+0x4a4..+0x544).

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace hailstone::net {
struct Item;
}

namespace hailstone::game::mechanics {

// The 56 rows of the stat table, in the original's order (the getter
// ItemBase_Get<name>; the damage bonuses by their skill).
enum class ItemStat : std::uint8_t {
    Str, Sta, Cha, Dex, Int, Agi, Wis, SvMagic, SvCold, SvFire, SvPoison, SvDisease, SvCorruption,
    Hp, Mana, Endurance, CombatEffects, Shielding, SpellShield, Avoidance, Accuracy, StunResist, DotShielding,
    DamageShieldMitigation, HeroicStr, HeroicInt, HeroicWis, HeroicAgi, HeroicDex, HeroicSta, HeroicCha,
    HeroicSvMagic, HeroicSvFire, HeroicSvCold, HeroicSvDisease, HeroicSvPoison, HeroicSvCorruption, HealAmount,
    SpellDamage, Clairvoyance, StrikeThrough, Attack, HpRegen, ManaRegen, EnduranceRegen, DamageShield, PotionSlots,
    DamageBonusBash, DamageBonusBackstab, DamageBonusDragonPunch, DamageBonusEagleStrike, DamageBonusFlyingKick,
    DamageBonusKick, DamageBonusRoundKick, DamageBonusTigerClaw, DamageBonusFrenzy,
};
inline constexpr int kItemStatCount = 56;
// The damage bonus rows' skills (0xa, 8, 0x15, 0x17, 0x1a, 0x1e, 0x26, 0x34, 0x4a).
inline constexpr std::array<std::int32_t, 9> kDamageBonusSkills = {10, 8, 21, 23, 26, 30, 38, 52, 74};

// What ProcessAllStats asks of one item (the ItemBase getters with the
// original's arguments).
struct StatItem {
    std::int32_t kind = 0;            // ItemBase_GetType: 0 a plain item (only those count)
    std::int32_t required_level = 0;  // GetRequiredLevel(1)
    std::uint32_t races = 0;          // GetRaces(1, 0): bit (race index - 1)
    std::uint32_t deities = 0;        // GetDeity(1, 0): bit (deity index - 1)
    std::int32_t prestige = 0;        // GetPrestige(0)
    std::int32_t charm_file_id = 0;   // GetCharmFileID
    std::int32_t recommended_skill = 0; // GetRecommendedSkill(1)
    std::int32_t recommended_level = 0; // GetRecommendedLevel(1)
    std::int32_t item_type = 0;       // ItemClient::GetItemType
    std::array<std::int32_t, kItemStatCount> stats{};
    const net::Item* source = nullptr; // for the recommended-level scaler, when built from a real item
};

// StatItem of a received item (the getters over the item and its augments).
StatItem stat_item(const net::Item& item);

// The item lookups, in the order the original walks them; nullptr where a
// lookup finds nothing.
struct ItemSources {
    std::span<const StatItem* const> worn;     // unk_Container_GetItem slots 0..0x15
    std::span<const StatItem* const> source_b; // FUN_0042def0 (5)
    std::span<const StatItem* const> source_c; // FUN_0042df20 (10)
    std::span<const StatItem* const> own_4c;   // the character's virtual +0x4c (2)
    std::span<const StatItem* const> own_54;   // the character's virtual +0x54 (4)
};

// What the sums ask about the character.
class ItemSumQueries {
public:
    virtual ~ItemSumQueries() = default;
    virtual std::int32_t level() = 0;          // CharacterBase virtual +0x68
    virtual bool any_race_deity() = 0;         // CharacterBase virtual +0x34: skips race, deity, prestige
    virtual std::int32_t race() = 0;           // BaseProfile::Race (+0x3370)
    virtual bool has_pc_base() = 0;            // CharacterBase virtual +0x4c: the deity check needs one
    virtual std::int32_t deity() = 0;          // GetDeity of that PcBase
    virtual std::int32_t limit(std::int32_t which) = 0; // CharacterBase virtual +0 (0x13: prestige, -1 none)
    virtual std::uint8_t item_type_to_skill(std::int32_t item_type) = 0;
    // FUN_004424f0: the character's virtual +0x34 (the recommended-level scaler).
    virtual std::int32_t recommended_scale(const StatItem& item, std::int32_t value, std::uint32_t skill) = 0;
    // FUN_004437d0: the damage bonus slot (0..8) of a skill.
    virtual std::uint32_t damage_slot(std::int32_t skill) = 0;
};

// FUN_004437d0.
std::uint32_t damage_bonus_slot(std::int32_t skill);

struct ItemSums {
    std::array<std::int32_t, 0x19c / 4> own{};             // CharacterZoneClient +0x00..+0x198
    std::array<std::int32_t, (0x548 - 0x4a4) / 4> base{};  // CharacterBase +0x4a4..+0x544
    std::int32_t& at(std::uint32_t offset) { return own[offset / 4]; }
    std::int32_t at(std::uint32_t offset) const { return own[offset / 4]; }
    std::int32_t& base_at(std::uint32_t offset) { return base[(offset - 0x4a4) / 4]; }
    std::int32_t base_at(std::uint32_t offset) const { return base[(offset - 0x4a4) / 4]; }
};

// ClearSums, the worn items, KeepWornSums and the other lookups (the pack
// items, CharacterZoneClient::sub_0044b150, are the caller's: not rewritten).
// The endurance upkeep (+0x94) is left as it is.
void sum_item_stats(ItemSums& sums, const ItemSources& sources, ItemSumQueries& q);

// DeriveStats' start: HP not below 0, the raw copies (+0xa0..+0xc4, the
// damage bonuses to +0x70..), then each bonus through the character's
// virtual +0x40 (value, kind). Item haste and the Calc* functions follow.
void derive_item_bonuses(ItemSums& sums, const std::function<std::int32_t(std::int32_t value, int kind)>& adjust);

} // namespace hailstone::game::mechanics
