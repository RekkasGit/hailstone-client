// Combat abilities (disciplines): PcZoneClient::doCombatAbility's checks, reuse messages and cast, Cmd_discipline,
// and the spell's deity mask. (The NMS patch's band table: the NMS plugin's nms_rules_test.cpp.)
#include <doctest/doctest.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "game/mechanics/combat_ability.h"
#include "game/spell_record.h"
#include "game/spells.h"

using namespace nmsc;
using namespace nmsc::game::mechanics::combat_ability;

namespace {

struct Said {
    std::uint32_t eqstr = 0;
    std::int32_t color = 0;
    std::vector<std::string> args;
};

// A Warrior (level 50, agnostic) with the given abilities and spells.
struct Fake : Queries {
    std::map<std::int32_t, Spell> spells;
    std::vector<std::int32_t> abilities;
    std::map<std::int32_t, std::uint8_t> level_needed; // by spell id; 1 when absent
    std::uint32_t cls = 1;
    std::int32_t lvl = 50;
    std::uint32_t god = 396;
    std::int32_t rank_limit = -1;
    bool alt_attack = false;
    std::array<std::uint32_t, kTimers> timers{};
    std::uint32_t now = 1000000;
    bool active = false;
    std::vector<Said> said;
    std::vector<std::int32_t> cast;

    Spell& add(std::int32_t id, const std::string& name) {
        Spell& s = spells[id];
        s.id = id;
        s.name = name;
        s.is_skill = 1;
        s.recast_time = 60000;
        abilities.push_back(id);
        return s;
    }
    const Spell* spell_by_id(std::int32_t id) override {
        auto it = spells.find(id);
        return it == spells.end() ? nullptr : &it->second;
    }
    const Spell* spell_by_group_and_rank(std::int32_t group, std::int32_t subgroup, std::int32_t rank,
                                         std::int32_t) override {
        for (auto& [id, s] : spells)
            if (s.group == group && s.subgroup == subgroup && s.rank == rank) return &s;
        return nullptr;
    }
    std::int32_t combat_ability(std::int32_t i) override {
        return i < static_cast<std::int32_t>(abilities.size()) ? abilities[static_cast<std::size_t>(i)] : 0;
    }
    bool character_virtual_90() override { return false; }
    std::int32_t character_rank(std::int32_t) override { return rank_limit; }
    std::uint8_t spell_level_needed(const Spell& s, std::uint32_t) override {
        auto it = level_needed.find(s.id);
        return it == level_needed.end() ? 1 : it->second;
    }
    std::uint32_t class_id() override { return cls; }
    std::int32_t level() override { return lvl; }
    std::uint32_t deity() override { return god; }
    bool take_alt_attack() override {
        bool was = alt_attack;
        alt_attack = false;
        return was;
    }
    std::uint32_t combat_ability_timer(std::int32_t id) override { return timers[static_cast<std::size_t>(id)]; }
    std::uint32_t discipline_reuse_timer(std::int32_t id) override {
        return id >= 0 && id < kTimers ? timers[static_cast<std::size_t>(id)] : 0;
    }
    std::uint32_t fast_time() override { return now; }
    bool discipline_active() override { return active; }
    void eqstr_template(std::uint32_t eqstr, std::int32_t color, const std::vector<std::string>& args) override {
        said.push_back({eqstr, color, args});
    }
    void message(std::uint32_t eqstr, std::int32_t color) override { said.push_back({eqstr, color, {}}); }
    void dsp_chat_2(std::uint32_t eqstr) override { said.push_back({eqstr, -1, {}}); }
    void cast_spell(std::int32_t id) override { cast.push_back(id); }
};

} // namespace

TEST_CASE("doCombatAbility casts a known, usable ability from slot 13") {
    Fake q;
    q.add(4514, "Mighty Strike");
    CHECK(do_combat_ability(4514, true, q));
    CHECK(q.cast == std::vector<std::int32_t>{4514});
    CHECK(q.said.empty());
    CHECK(kCastSlot == 13);
}

TEST_CASE("doCombatAbility refuses what CanUseMeleeCombatAbility refuses") {
    Fake q;
    Spell& s = q.add(100, "Fury");
    CHECK(can_use_melee_combat_ability(100, q) == 1);
    CHECK(can_use_melee_combat_ability(999, q) == 0); // no such spell
    q.level_needed[100] = 0xff;
    CHECK(can_use_melee_combat_ability(100, q) == -1);
    q.level_needed[100] = 51;
    CHECK(can_use_melee_combat_ability(100, q) == -2);
    q.level_needed[100] = 50;
    s.deity_mask = 1u << 2; // deity 202 only
    CHECK(can_use_melee_combat_ability(100, q) == -3);
    q.god = 202;
    CHECK(can_use_melee_combat_ability(100, q) == 1);
    s.is_skill = 0;
    CHECK(can_use_melee_combat_ability(100, q) == 0);
    CHECK_FALSE(do_combat_ability(100, true, q));
    CHECK(q.cast.empty());
}

TEST_CASE("CanUseMeleeCombatAbility: a known higher rank, and the rank limit") {
    Fake q;
    Spell& r1 = q.add(10, "Rage");
    Spell& r2 = q.add(11, "Rage Rk. II");
    r1.group = r2.group = 7;
    r1.rank = 1;
    r2.rank = 2;
    CHECK(can_use_melee_combat_ability(10, q) == -4);
    CHECK(can_use_melee_combat_ability(11, q) == 1);
    q.rank_limit = 1; // the higher rank is above the limit: rank 1 is the one to use
    CHECK(can_use_melee_combat_ability(10, q) == 1);
    CHECK(can_use_melee_combat_ability(11, q) == -5);
}

TEST_CASE("doCombatAbility's rank swap uses the group's rank at the limit") {
    Fake q;
    Spell& r1 = q.add(10, "Rage");
    Spell& r2 = q.add(11, "Rage Rk. II");
    r1.group = r2.group = 7;
    r1.rank = 1;
    r2.rank = 2;
    q.rank_limit = 1;
    CHECK(do_combat_ability(11, true, q));
    CHECK(q.cast == std::vector<std::int32_t>{10});
    q.cast.clear();
    CHECK_FALSE(do_combat_ability(11, false, q)); // no swap: rank 2 is above the limit
}

TEST_CASE("HasCombatAbility: the spell, or a same-group ability of at least its rank") {
    Fake q;
    Spell& known = q.add(20, "Known Rk. III");
    known.group = 9;
    known.rank = 3;
    q.spells[21] = Spell{.id = 21, .group = 9, .rank = 2};
    q.spells[22] = Spell{.id = 22, .group = 9, .rank = 4};
    q.spells[23] = Spell{.id = 23};
    CHECK(has_combat_ability(20, q));
    CHECK(has_combat_ability(21, q));
    CHECK_FALSE(has_combat_ability(22, q));
    CHECK_FALSE(has_combat_ability(23, q));
}

TEST_CASE("doCombatAbility on its reuse timer prints the time left and refuses") {
    Fake q;
    Spell& s = q.add(30, "Bulwark");
    s.timer = 4;
    s.recast_time = 600000; // 600 s
    q.timers[4] = q.now + 125;
    CHECK_FALSE(do_combat_ability(30, true, q));
    REQUIRE(q.said.size() == 1);
    CHECK(q.said[0].eqstr == kEqstrReadyMinutes);
    CHECK(q.said[0].color == 0x10f);
    CHECK(q.said[0].args == std::vector<std::string>{"Bulwark", "2", "5"});

    q.said.clear();
    q.timers[4] = q.now + 3 * 3600 + 7 * 60 + 9;
    CHECK_FALSE(do_combat_ability(30, true, q));
    REQUIRE(q.said.size() == 1);
    CHECK(q.said[0].eqstr == kEqstrReadyHours);
    CHECK(q.said[0].args == std::vector<std::string>{"Bulwark", "3", "7", "9"});

    q.said.clear();
    q.timers[4] = q.now; // ready at now: no message
    CHECK(do_combat_ability(30, true, q));
    CHECK(q.said.empty());
}

TEST_CASE("doCombatAbility: the seconds part alone is compared with the recast time") {
    // The original goes on when (time left mod 60) > RecastTime / 1000, after printing the time left.
    Fake q;
    Spell& s = q.add(31, "Quick");
    s.timer = 2;
    s.recast_time = 30000;
    q.timers[2] = q.now + 45;
    CHECK(do_combat_ability(31, true, q));
    CHECK(q.said.size() == 1);
    CHECK(q.cast == std::vector<std::int32_t>{31});
}

TEST_CASE("doCombatAbility: timer ids from 20 up are never on the stock timer") {
    Fake q;
    Spell& s = q.add(32, "Banded");
    s.timer = 23;
    CHECK(do_combat_ability(32, true, q));
    s.timer = -1; // unsigned: above 19
    CHECK(do_combat_ability(32, true, q));
}

TEST_CASE("doCombatAbility refuses a duration self ability while a discipline runs") {
    Fake q;
    Spell& s = q.add(40, "Defensive");
    s.duration_formula = 1;
    s.target_type = 6;
    q.active = true;
    CHECK_FALSE(do_combat_ability(40, true, q));
    REQUIRE(q.said.size() == 1);
    CHECK(q.said[0].eqstr == kEqstrAlreadyActive);
    s.target_type = 5; // not a self spell
    CHECK(do_combat_ability(40, true, q));
}

TEST_CASE("doCombatAbility with AltAttack set skips the checks and clears it") {
    Fake q;
    q.spells[50] = Spell{.id = 50, .name = "Unknown"};
    CHECK_FALSE(do_combat_ability(50, true, q));
    q.alt_attack = true;
    CHECK(do_combat_ability(50, true, q));
    CHECK_FALSE(q.alt_attack);
}

TEST_CASE("Cmd_discipline: the class gate, a name prefix and the timer list") {
    Fake q;
    q.add(60, "Mighty Strike");
    Spell& b = q.add(61, "Bulwark of Alacrity");
    b.timer = 3;
    q.timers[3] = q.now + 61;

    discipline_command("BULWARK", q);
    CHECK(q.cast.empty()); // on its timer: refused with the time left
    REQUIRE(q.said.size() == 1);
    CHECK(q.said[0].args == std::vector<std::string>{"Bulwark of Alacrity", "1", "1"});

    q.said.clear();
    discipline_command("mig", q);
    CHECK(q.cast == std::vector<std::int32_t>{60});

    q.cast.clear();
    discipline_command("Mighty Strike and more", q); // longer than the name: no match
    CHECK(q.cast.empty());

    discipline_command("", q);
    REQUIRE(q.said.size() == 1);
    CHECK(q.said[0].eqstr == kEqstrReadyMinutes);

    q.said.clear();
    q.cls = 2; // Cleric
    discipline_command("mig", q);
    REQUIRE(q.said.size() == 1);
    CHECK(q.said[0].eqstr == kEqstrWrongClass);
    CHECK(q.cast.empty());
}

TEST_CASE("Cmd_discipline calls doCombatAbility through its address (a server patch's detour runs)") {
    struct Detoured : Fake {
        std::vector<std::int32_t> used;
        void use_combat_ability(std::int32_t id) override { used.push_back(id); }
    };
    Detoured q;
    q.add(60, "Mighty Strike");
    discipline_command("mig", q);
    CHECK(q.used == std::vector<std::int32_t>{60});
    CHECK(q.cast.empty());
}

TEST_CASE("GetDeityBitmask") {
    CHECK(deity_bitmask(396) == 1u);
    CHECK(deity_bitmask(201) == 2u);
    CHECK(deity_bitmask(216) == 1u << 16);
    CHECK(deity_bitmask(0) == 1u << ((0u - 200u) & 31));
}

TEST_CASE("SpellData's deity mask is the record's +0x15c") {
    std::vector<std::string> f(237, "0");
    f[0] = "4514";
    f[1] = "Mighty Strike";
    f[126] = "1"; // deity 201
    f[141] = "1"; // deity 216
    std::string line;
    for (std::size_t i = 0; i < f.size(); ++i) line += (i ? "^" : "") + f[i];
    auto s = game::SpellTable::parse_line(line);
    REQUIRE(s);
    CHECK(s->deity_mask == (1u << 1 | 1u << 16));
    const game::SpellRecord r = game::spell_record_from_line(line);
    std::uint32_t mask = 0;
    std::memcpy(&mask, r.data() + 0x15c, 4);
    CHECK(s->deity_mask == mask);
}
