// Spell sets (game/spell_sets.h): SaveLoadout, SortSpellLoadouts, SaveSpellLoadoutsToIni and /memspellset's lookup.
#include <doctest/doctest.h>

#include <array>

#include "core/ini_file.h"
#include "game/spell_sets.h"

using namespace hailstone;

namespace {

std::array<std::int32_t, game::kLoadoutGems> gems(std::int32_t first) {
    std::array<std::int32_t, game::kLoadoutGems> g;
    g.fill(-1);
    g[0] = first;
    return g;
}

} // namespace

TEST_CASE("spell sets: saved by name, sorted in use first by name, case ignored") {
    game::SpellLoadouts s;
    int at = -1;
    CHECK(s.save_set("Raid", gems(1), &at) == game::SpellLoadouts::SaveResult::Saved);
    CHECK(at == 0);
    s.save_set("buffs", gems(2), &at);
    CHECK(at == 0); // "buffs" < "Raid" (__stricmp)
    CHECK(s.at(0).name == "buffs");
    CHECK(s.at(1).name == "Raid");
    s.save_set("RAID", gems(3), &at); // the same set (case ignored) takes the new name and gems
    CHECK(at == 1);
    CHECK(s.at(1).name == "RAID");
    CHECK(s.at(1).spells[0] == 3);
    CHECK_FALSE(s.at(2).in_use);
    CHECK(s.save_set(std::string(40, 'x'), gems(4), &at) == game::SpellLoadouts::SaveResult::Saved);
    CHECK(s.at(at).name.size() == game::kLoadoutNameMax);
}

TEST_CASE("spell sets: ten at most") {
    game::SpellLoadouts s;
    for (int i = 0; i < game::kSpellLoadouts; ++i)
        CHECK(s.save_set("set" + std::to_string(i), gems(i)) == game::SpellLoadouts::SaveResult::Saved);
    CHECK(s.save_set("one more", gems(99)) == game::SpellLoadouts::SaveResult::Full);
    CHECK(s.save_set("SET3", gems(7)) == game::SpellLoadouts::SaveResult::Saved); // an existing name still saves
    CHECK(s.remove(0));
    CHECK_FALSE(s.at(game::kSpellLoadouts - 1).in_use); // the free set sorts last
}

TEST_CASE("spell sets: /memspellset's lookup") {
    game::SpellLoadouts s;
    s.save_set("Healing", gems(1));
    s.save_set("Nuking", gems(2));
    CHECK(s.find("1") == 0);
    CHECK(s.find("2") == 1);
    CHECK(s.find("3") == -1);   // not in use
    CHECK(s.find("11") == -1);
    // A number past 32 bits is read the same on every platform (core::msvc_atoi; std::atoi gave Linux 1, Windows
    // INT_MAX).
    CHECK(s.find("4294967297") == 0);
    CHECK(s.find("99999999999") == -1);
    CHECK(s.find("nuk") == 1);  // the start of the name, case ignored
    CHECK(s.find("Healingx") == -1);
    CHECK(s.find("") == -1);
}

TEST_CASE("spell sets: the ini keys, only changed sets written") {
    game::SpellLoadouts s;
    auto g = gems(1234);
    g[11] = 77;
    s.save_set("Main", g);
    IniFile ini;
    s.save(ini);
    CHECK(ini.get_int("SpellLoadouts", "SpellLoadout1.inuse", 0) == 1);
    CHECK(ini.get_string("SpellLoadouts", "SpellLoadout1.name") == "Main");
    CHECK(ini.get_int("SpellLoadouts", "SpellLoadout1.slot1", 0) == 1234);
    CHECK(ini.get_int("SpellLoadouts", "SpellLoadout1.slot2", 0) == -1);
    CHECK(ini.get_int("SpellLoadouts", "SpellLoadout1.slot12", 0) == 77);
    CHECK(ini.get_int("SpellLoadouts", "SpellLoadout2.inuse", 9) == 0); // the sort marks all ten changed
    CHECK_FALSE(ini.get("SpellLoadouts", "SpellLoadout2.name"));

    game::SpellLoadouts back;
    back.load(ini);
    CHECK(back.at(0).in_use);
    CHECK(back.at(0).name == "Main");
    CHECK(back.at(0).spells[11] == 77);
    CHECK_FALSE(back.take_dirty());
    ini.set("SpellLoadouts", "SpellLoadout1.name", "Changed");
    IniFile again = ini;
    back.save(again); // nothing changed since the load: nothing written
    CHECK(again.get_string("SpellLoadouts", "SpellLoadout1.name") == "Changed");
}

TEST_CASE("spell sets: the editor renames a set and gives it spells, sorted, nothing else changed") {
    game::SpellLoadouts s;
    s.save_set("Buffs", gems(1));
    s.save_set("Nukes", gems(2));
    s.take_dirty();
    auto g = gems(10);
    g[7] = 11;
    int at = -1;
    CHECK(s.edit(1, "Aggro", g, &at)); // "Nukes" renamed: it sorts first now
    CHECK(at == 0);
    CHECK(s.at(0).name == "Aggro");
    CHECK(s.at(0).spells[0] == 10);
    CHECK(s.at(0).spells[7] == 11);
    CHECK(s.at(1).name == "Buffs");
    CHECK(s.at(1).spells[0] == 1);
    CHECK_FALSE(s.at(2).in_use);
    CHECK(s.take_dirty());
    // the same name, other spells: it stays where it is
    CHECK(s.edit(0, "Aggro", gems(12), &at));
    CHECK(at == 0);
    CHECK(s.at(0).spells[0] == 12);
    // a long name is cut as SaveLoadout cuts it
    CHECK(s.edit(0, std::string(40, 'a'), gems(12), &at));
    CHECK(s.at(at).name.size() == game::kLoadoutNameMax);
}

TEST_CASE("spell sets: the editor naming a set after another replaces that one") {
    game::SpellLoadouts s;
    s.save_set("Buffs", gems(1));
    s.save_set("Nukes", gems(2));
    s.save_set("Pulls", gems(3));
    int at = -1;
    CHECK(s.edit(2, "NUKES", gems(30), &at)); // "Pulls" takes the name; the old "Nukes" is gone
    CHECK(at == 1);
    CHECK(s.at(0).name == "Buffs");
    CHECK(s.at(1).name == "NUKES");
    CHECK(s.at(1).spells[0] == 30);
    CHECK_FALSE(s.at(2).in_use);
    CHECK(s.find("nukes") == 1);
}

TEST_CASE("spell sets: the editor refuses a set not in use and an empty name") {
    game::SpellLoadouts s;
    s.save_set("Buffs", gems(1));
    s.take_dirty();
    CHECK_FALSE(s.edit(1, "Other", gems(2)));
    CHECK_FALSE(s.edit(-1, "Other", gems(2)));
    CHECK_FALSE(s.edit(game::kSpellLoadouts, "Other", gems(2)));
    CHECK_FALSE(s.edit(0, "", gems(2)));
    CHECK(s.at(0).name == "Buffs");
    CHECK(s.at(0).spells[0] == 1);
    CHECK_FALSE(s.take_dirty());
}

TEST_CASE("spell sets: an edited set is written to the ini as the stock client writes one") {
    game::SpellLoadouts s;
    s.save_set("Main", gems(5));
    IniFile ini;
    s.save(ini);
    auto g = gems(-1);
    g[11] = 99;
    REQUIRE(s.edit(0, "Raid", g));
    s.save(ini);
    CHECK(ini.get_int("SpellLoadouts", "SpellLoadout1.inuse", 0) == 1);
    CHECK(ini.get_string("SpellLoadouts", "SpellLoadout1.name") == "Raid");
    CHECK(ini.get_int("SpellLoadouts", "SpellLoadout1.slot1", 0) == -1);
    CHECK(ini.get_int("SpellLoadouts", "SpellLoadout1.slot12", 0) == 99);
    game::SpellLoadouts back;
    back.load(ini);
    CHECK(back.at(0).name == "Raid");
    CHECK(back.at(0).spells[11] == 99);
}
