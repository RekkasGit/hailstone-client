// The extended target list's client rules and the extended target window's role strings and role sets
// (game/xtarget.h), checked against the original's functions (rof2decomp export, addresses in the header).

#include <doctest/doctest.h>

#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "game/xtarget.h"

using namespace nmsc;
using namespace nmsc::game;

namespace {

XTargetState list_of(std::vector<std::uint32_t> types) {
    XTargetState l;
    for (auto t : types) {
        XTargetSlot s;
        s.type = t;
        l.slots.push_back(s);
    }
    return l;
}

} // namespace

TEST_CASE("xtarget: type names, parsing and role icon cells") {
    CHECK(std::string(xtarget_type_name(1)) == "Auto Hater");
    CHECK(std::string(xtarget_type_name(26)) == "Mercenary Target");
    CHECK(xtarget_type_name(27) == nullptr);
    CHECK(parse_xtarget_type("grouptank") == 5u);
    CHECK(parse_xtarget_type("GTT") == 6u);
    CHECK(parse_xtarget_type("CurrentTarget") == 2u);
    CHECK(parse_xtarget_type("ra3t") == 19u);
    CHECK(!parse_xtarget_type("SpecificNPC"));
    CHECK(!parse_xtarget_type("Lorudan"));
    CHECK(xtarget_role_icon_cell(0) == -1);  // Empty Target: no icon
    CHECK(xtarget_role_icon_cell(1) == 30);  // AUTO HATE
    CHECK(xtarget_role_icon_cell(2) == 27);  // PC
    CHECK(xtarget_role_icon_cell(3) == 28);  // NPC
    CHECK(xtarget_role_icon_cell(5) == 2);   // the tank's shield
    CHECK(xtarget_role_icon_cell(20) == 13); // raid mark 1
    CHECK(xtarget_role_icon_cell(27) == -1);
}

TEST_CASE("xtarget: slot searches and slot changes") {
    XTargetState l = list_of({1, 3, 1, 2, 5});
    l.slots[1].spawn_id = 40;
    l.slots[3].spawn_id = 40;
    CHECK(xtarget_first_auto_hater_slot(l) == 0);
    CHECK(xtarget_last_specific_slot(l) == 3);
    CHECK(xtarget_specific_slot_by_spawn(l, 40) == 1);
    CHECK(xtarget_specific_slot_by_spawn(l, 41) == -1);
    CHECK(xtarget_first_auto_hater_slot(list_of({5, 6})) == -1);

    // Send_OP_XTargetRequest: same type and name (any case) sends nothing; a change clears status and spawn.
    l.slots[3].name = "Lorudan";
    CHECK_FALSE(xtarget_set_slot(l, 3, 2, "lorudan"));
    CHECK(xtarget_set_slot(l, 3, 1, ""));
    CHECK(l.slots[3].type == 1);
    CHECK(l.slots[3].spawn_id == 0);
    CHECK(l.slots[3].name.empty());
    CHECK_FALSE(xtarget_set_slot(l, 5, 1, ""));  // past the slots
    CHECK_FALSE(xtarget_set_slot(l, -1, 1, ""));
}

TEST_CASE("xtarget: role strings build and apply as the original's") {
    XTargetState l = list_of({1, 5, 2, 1, 3});
    l.slots[2].name = "Lorudan";
    l.slots[4].name = "a_rat00";
    std::string s = xtarget_role_string(l, "Autosave", 1);
    CHECK(s == "1^Autosave^3^1^5^2^2^Lorudan^4^3");
    CHECK(xtarget_role_string_name(s) == "Autosave");
    CHECK(xtarget_role_string_name("0^x^0").empty());

    std::vector<std::tuple<int, std::uint32_t, std::string>> sent;
    auto send = [&](int slot, std::uint32_t type, const std::string& name) { sent.emplace_back(slot, type, name); };
    std::string name;
    REQUIRE(xtarget_apply_role_string(s, 6, &name, send));
    CHECK(name == "Autosave");
    // Slot 1 tank, slot 2 the PC by name, slot 4 (a specific NPC) not restored but listed; the rest auto hater.
    std::vector<std::tuple<int, std::uint32_t, std::string>> want = {
        {1, 5, ""}, {2, 2, "Lorudan"}, {0, 1, ""}, {3, 1, ""}, {5, 1, ""}};
    CHECK(sent == want);

    sent.clear();
    CHECK_FALSE(xtarget_apply_role_string("", 5, nullptr, send)); // no saved roles: nothing
    CHECK(sent.empty());

    // A forged count: no more requests than the window has rows (plus the auto haters for unlisted slots).
    REQUIRE(xtarget_apply_role_string("1^Big^2000000000^0^5", 3, nullptr, send));
    CHECK(sent.size() == static_cast<std::size_t>(kXTargetMaxSlots) + 2);
}

TEST_CASE("xtarget: role sets load, sort and save in the character ini") {
    std::map<std::string, std::string> ini = {{"TargetSet1", "1^Zeta^1^0^5"}, {"TargetSet3", "1^alpha^0"}};
    XTargetRoleSets sets;
    sets.load([&](const std::string& k) { return ini.count(k) ? ini[k] : std::string(); });
    CHECK(sets.sets[0].used);
    CHECK(sets.sets[0].name == "Zeta");
    CHECK_FALSE(sets.sets[1].used);
    CHECK(sets.sets[2].name == "alpha");
    CHECK(sets.slot_for_name("ZETA") == 0);
    CHECK(sets.slot_for_name("new") == 1); // the first unused

    std::map<std::string, std::string> out;
    sets.sort_and_save([&](const std::string& k, const std::string& v) { out[k] = v; });
    CHECK(sets.sets[0].name == "alpha");
    CHECK(sets.sets[1].name == "Zeta");
    CHECK(out.size() == 10);
    CHECK(out["TargetSet1"] == "1^alpha^0");
    CHECK(out["TargetSet2"] == "1^Zeta^1^0^5");
    CHECK(out["TargetSet3"].empty());

    for (auto& s : sets.sets) s.used = true;
    CHECK(sets.slot_for_name("other") == -1);
}
