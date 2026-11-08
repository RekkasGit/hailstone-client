// Ctrl moves into a bag (game/bag_move.h): MoveItem's list and ProcessMove's room search.

#include <doctest/doctest.h>

#include "game/bag_move.h"

using namespace hailstone;
using game::BagMove;

namespace {

net::Item bag(const char* name, int slots, int size = 4) {
    net::Item b;
    b.id = 17005;
    b.name = name;
    b.item_class = 1;
    b.bag_slots = static_cast<std::uint8_t>(slots);
    b.bag_size = static_cast<std::uint8_t>(size);
    return b;
}

net::Item thing(std::uint32_t id, const char* name, std::uint32_t count = 1, std::uint32_t max_stack = 0, int size = 1) {
    net::Item it;
    it.id = id;
    it.name = name;
    it.stack_count = count;
    it.max_stack = max_stack;
    it.size = static_cast<std::uint8_t>(size);
    return it;
}

void put(net::Item& b, int sub, net::Item it) { b.contents.push_back({static_cast<std::uint32_t>(sub), std::move(it)}); }

} // namespace

TEST_CASE("bag move: a single item goes to the first free slot") {
    net::Item target = bag("Backpack", 4);
    put(target, 0, thing(1, "Rock"));
    put(target, 2, thing(2, "Stick"));
    auto p = game::plan_bag_move(thing(3, "Cap"), target, false);
    REQUIRE(p.eqstr == 0);
    REQUIRE(p.moves.size() == 1);
    CHECK(p.moves[0].from == BagMove::Where::Cursor);
    CHECK(p.moves[0].to_sub == 1);
}

TEST_CASE("bag move: a stack joins the stacks in the bag first, splitting over them") {
    net::Item target = bag("Backpack", 4);
    put(target, 1, thing(10, "Bone Chips", 18, 20));
    put(target, 3, thing(10, "Bone Chips", 15, 20));
    auto p = game::plan_bag_move(thing(10, "Bone Chips", 9, 20), target, false);
    REQUIRE(p.eqstr == 0);
    REQUIRE(p.moves.size() == 3);
    CHECK(p.moves[0].to_sub == 1); // 2 fit on top of the 18
    CHECK(p.moves[0].count == 2);
    CHECK(p.moves[1].to_sub == 3); // 5 of the other 7 on the 15
    CHECK(p.moves[1].count == 5);
    CHECK(p.moves[1].from == BagMove::Where::Cursor);
    CHECK(p.moves[2].to_sub == 0); // the last 2 in the first free slot
    CHECK(p.moves[2].count == 2);
    // Full stacks are passed over: the stack takes the first free slot.
    target = bag("Backpack", 4);
    put(target, 0, thing(10, "Bone Chips", 20, 20));
    p = game::plan_bag_move(thing(10, "Bone Chips", 5, 20), target, false);
    REQUIRE(p.moves.size() == 1);
    CHECK(p.moves[0].to_sub == 1);
}

TEST_CASE("bag move: a cursor bag's contents in slot order, and the refusals") {
    net::Item cursor = bag("Small Bag", 4);
    put(cursor, 3, thing(1, "Rock"));
    put(cursor, 1, thing(2, "Stick"));
    net::Item target = bag("Backpack", 4);
    put(target, 0, thing(5, "Cap"));
    auto p = game::plan_bag_move(cursor, target, false);
    REQUIRE(p.moves.size() == 2);
    CHECK(p.moves[0].from_sub == 1);
    CHECK(p.moves[0].to_sub == 1);
    CHECK(p.moves[1].from_sub == 3);
    CHECK(p.moves[1].to_sub == 2);
    // No room: 5214.
    put(target, 1, thing(6, "a"));
    put(target, 2, thing(7, "b"));
    p = game::plan_bag_move(cursor, target, false);
    CHECK(p.eqstr == 5214);
    CHECK(p.moves.empty());
    // Too big: 12934 with the cursor item and the bag for a one-entry list, else 5215.
    target = bag("Backpack", 4, 1);
    p = game::plan_bag_move(thing(1, "Spear", 1, 0, 3), target, false);
    CHECK(p.eqstr == 12934);
    CHECK(p.args == std::vector<std::string>{"Spear", "Backpack"});
    net::Item one = bag("Small Bag", 4);
    put(one, 0, thing(1, "Spear", 1, 0, 3));
    p = game::plan_bag_move(one, target, false);
    CHECK(p.eqstr == 12934);
    CHECK(p.args == std::vector<std::string>{"Small Bag", "Backpack"});
    put(one, 1, thing(2, "Stick"));
    p = game::plan_bag_move(one, target, false);
    CHECK(p.eqstr == 5215);
    // An empty cursor bag without force moves into the bag whole.
    p = game::plan_bag_move(bag("Small Bag", 4, 2), bag("Backpack", 4), false);
    REQUIRE(p.moves.size() == 1);
    CHECK(p.moves[0].from == BagMove::Where::Cursor);
}

TEST_CASE("bag move: Ctrl + right-click trades the contents, each bag packed from its first slot") {
    net::Item cursor = bag("Small Bag", 4);
    put(cursor, 2, thing(1, "Rock"));
    net::Item target = bag("Backpack", 4);
    put(target, 1, thing(2, "Stick"));
    put(target, 3, thing(3, "Cap"));
    auto p = game::plan_bag_move(cursor, target, true);
    REQUIRE(p.eqstr == 0);
    REQUIRE(p.moves.size() == 3);
    CHECK(p.moves[0].from == BagMove::Where::CursorBag);
    CHECK_FALSE(p.moves[0].to_cursor_bag);
    CHECK(p.moves[0].to_sub == 0);
    CHECK(p.moves[1].from == BagMove::Where::TargetBag);
    CHECK(p.moves[1].from_sub == 1);
    CHECK(p.moves[1].to_cursor_bag);
    CHECK(p.moves[1].to_sub == 0);
    CHECK(p.moves[2].from_sub == 3);
    CHECK(p.moves[2].to_sub == 1);
    // An empty cursor bag takes the target's items.
    p = game::plan_bag_move(bag("Small Bag", 4), target, true);
    REQUIRE(p.moves.size() == 2);
    CHECK(p.moves[0].to_cursor_bag);
    // More than the other bag holds: 5214.
    net::Item small = bag("Tiny Bag", 1);
    p = game::plan_bag_move(small, target, true);
    CHECK(p.eqstr == 5214);
}
