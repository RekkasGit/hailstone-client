// The movement frame's character inputs (game/mechanics/movement_inputs.h) on synthetic characters: each
// value must come from the exact CharacterZoneClient ports (TotalEffect with buffs and items, STR / STA with
// items and buffs, adjusted skills, heroic AGI, CalcCarriedWeight with bag reductions, Cur_HP /
// Max_HP), not from the profile or the buffs' base values. No EQ files are used.
#include <doctest/doctest.h>

#include <array>
#include <map>

#include "game/mechanics/character.h"
#include "game/mechanics/movement.h"
#include "game/mechanics/movement_inputs.h"
#include "game/mechanics/tables.h"
#include "game/spells.h"

using namespace hailstone;
using namespace hailstone::game;
using namespace hailstone::game::mechanics;

namespace {

constexpr const char* kBaseData =
    "1^1^25^0^15^2^5^0.083^0^0.075\n"
    "60^1^400^0^300^3^6^0.9^0^1.25\n";

struct Rig {
    GameTables tables;
    std::map<std::int32_t, SpellData> spells;
    Rig() { tables.class_levels.parse(kBaseData); }
    SpellLookup lookup() {
        return [this](std::int32_t id) -> const SpellData* {
            auto it = spells.find(id);
            return it == spells.end() ? nullptr : &it->second;
        };
    }
    // A spell whose effect slots are blank (254) except the given {spa, base, formula, base2} ones.
    void spell(std::int32_t id, std::initializer_list<std::array<std::int32_t, 4>> effects) {
        SpellData s;
        s.id = static_cast<std::uint32_t>(id);
        s.effect.fill(254);
        s.formula.fill(100);
        s.duration_formula = 50;
        s.spell_type = 1;
        s.class_level.fill(255);
        std::size_t k = 0;
        for (const auto& e : effects) {
            s.effect[k] = e[0];
            s.base[k] = e[1];
            s.formula[k] = e[2];
            s.base2[k] = e[3];
            ++k;
        }
        spells[id] = s;
    }
};

CharacterSheet warrior(int level) {
    CharacterSheet s;
    s.race = 1;
    s.class_id = 1;
    s.level = level;
    s.highest_level = level;
    s.base_str = s.base_sta = s.base_cha = s.base_dex = s.base_int = s.base_agi = s.base_wis = 75;
    s.stored_hp = 100000;
    s.buffs.resize(kPlayerBuffSlots);
    return s;
}

net::Item item() {
    net::Item i;
    i.item_class = 0;
    i.races = 0xffff;
    i.classes = 0xffff;
    return i;
}

SheetBuff buff(std::int32_t spell) {
    SheetBuff b;
    b.type = 2;
    b.level = 60;
    b.spell_id = spell;
    b.duration = 100;
    return b;
}

MovementFacts facts_of(Rig& rig, const CharacterSheet& sheet) {
    Character ch(sheet, rig.tables, rig.lookup());
    ch.process_all_stats(); // StatsUi does it each frame before the movement update
    return movement_facts(ch);
}

} // namespace

TEST_CASE("movement facts: TotalEffect sums the buffs' evaluated values, not their base values") {
    Rig rig;
    rig.spell(10, {{3, 40, 100, 0}});   // movement rate (SoW-like)
    rig.spell(11, {{99, -30, 100, 0}}); // a snare in the SPA 99 slot
    rig.spell(12, {{290, 15, 100, 0}});
    CharacterSheet s = warrior(60);
    s.buffs[0] = buff(10);
    s.buffs[1] = buff(11);
    s.buffs[2] = buff(12);
    MovementFacts f = facts_of(rig, s);
    CHECK(f.spa3 == 40);
    CHECK(f.spa99 == -30);
    CHECK(f.spa290 == 15);
    CHECK(f.spa271 == 0);
    // Nothing to evaluate: all zero.
    MovementFacts none = facts_of(rig, warrior(60));
    CHECK(none.spa3 == 0);
    CHECK(none.spa99 == 0);
}

TEST_CASE("movement facts: STR and STA with items and buffs are the capacity and the air supply inputs") {
    Rig rig;
    rig.spell(20, {{4, 20, 100, 0}, {7, 10, 100, 0}}); // STR +20, STA +10
    CharacterSheet s = warrior(60);
    net::Item helm = item();
    helm.astr = 5;
    helm.asta = 3;
    s.possessions[2] = helm;
    MovementFacts items_only = facts_of(rig, s);
    CHECK(items_only.strength == 80);
    CHECK(items_only.stamina == 78);
    s.buffs[0] = buff(20);
    MovementFacts f = facts_of(rig, s);
    CHECK(f.strength == 100);
    CHECK(f.stamina == 88);
    // The capacity moves the weight factor: 8.0 carried, 100 capacity, no penalty; the same weight against 80.
    WeightInput w;
    w.hp = w.max_hp = 1000;
    w.level = 60;
    w.carried = 90;
    w.strength = f.strength;
    CHECK(calculate_weight(w) == 1.0f);
    w.strength = items_only.strength - 40; // a profile-only STR would have been 75
    CHECK(calculate_weight(w) < 1.0f);
}

TEST_CASE("movement facts: carried weight counts bag reductions and SPA 221, not augments") {
    Rig rig;
    rig.spell(30, {{221, 50, 100, 0}}); // weight reduction 50%
    CharacterSheet s = warrior(60);
    net::Item sword = item();
    sword.weight = 30;
    // An augment socketed into the sword adds nothing: ItemBase_GetWeight recurses only for an item without a
    // definition.
    net::BagItem aug;
    aug.index = 0;
    aug.item = item();
    aug.item.weight = 20;
    sword.contents.push_back(aug);
    s.possessions[13] = sword;
    net::Item bag;
    bag.item_class = 1;
    bag.weight = 10;
    bag.weight_reduction = 50;
    bag.bag_slots = 10;
    net::BagItem inside;
    inside.index = 0;
    inside.item = item();
    inside.item.weight = 100;
    bag.contents.push_back(inside);
    s.possessions[23] = bag;
    MovementFacts f = facts_of(rig, s);
    // 30 / 10 = 3, the bag 1, its contents 10 x 50% = 5: 9
    CHECK(f.carried == 9);
    s.buffs[0] = buff(30);
    CHECK(facts_of(rig, s).carried == 4); // 9 less 50%, truncated
}

TEST_CASE("movement facts: skills, heroic agility and the levitation cache come from the character") {
    Rig rig;
    rig.spell(40, {{57, 1, 100, 0}});
    rig.spell(41, {{57, 1, 100, 1}});
    CharacterSheet s = warrior(60);
    s.skills[39] = 70;
    s.skills[50] = 90;
    s.expansions = 0x2000;
    net::Item boots = item();
    boots.heroic_agi = 7;
    s.possessions[19] = boots;
    s.buffs[0] = buff(40);
    MovementFacts f = facts_of(rig, s);
    CHECK(f.safe_fall_base == 70);
    CHECK(f.safe_fall_adjusted >= 0);
    CHECK(f.swimming_skill >= 0);
    CHECK(f.heroic_agi == 7);
    CHECK(f.levitate_mode2 != 0);
    CHECK(f.levitate_mode5 == 0);
    s.buffs[0] = buff(41);
    f = facts_of(rig, s);
    CHECK(f.levitate_mode2 == 0);
    CHECK(f.levitate_mode5 != 0);
    // Without the heroic expansion bit the bonus is 0 (players only).
    s.expansions = 0;
    CHECK(facts_of(rig, s).heroic_agi == 0);
}

TEST_CASE("movement facts: Cur_HP and Max_HP feed the weight factor, CharacterBase +0x460 its level threshold") {
    Rig rig;
    CharacterSheet s = warrior(2);
    MovementFacts f = facts_of(rig, s);
    CHECK(f.max_hp > 0);
    CHECK(f.hp <= f.max_hp);
    CHECK_FALSE(f.low_level_exempt_off);
    s.slowdown_any_level = true;
    CHECK(facts_of(rig, s).low_level_exempt_off);
}
