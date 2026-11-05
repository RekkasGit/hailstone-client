#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "game/eq_strings.h"
#include "game_files.h"

TEST_CASE("eqstr templates fill %n, %Tn and %Bn(type)" * doctest::skip(!nmsc::test::game_dir())) {
    nmsc::game::EqStrings s;
    s.load(*nmsc::test::game_dir());
    REQUIRE(s.loaded());
    std::vector<std::string> args{"1597", "0", "1215"};
    // %T3: argument 3 is an eqstr id (1215 "points"), expanded (FormatEqStrTemplate @0x4e1540).
    CHECK(s.format(410, args) == "You have gained the ability \"Call of Challenge\" at a cost of 0 ability points.");
    // NPC SayString: 554 "%1 says '%T2'" with the inner string's own %3/%4 from the same arguments.
    std::vector<std::string> say{"Rabley Trumend", "1147", "Nmscshoptest", "Iron Ration"};
    CHECK(s.format(554, say) ==
          "Rabley Trumend says 'Hi there Nmscshoptest, just browsing?  Have you seen the Iron Ration I just got in?'");
    CHECK(s.format(999999999, args) == "[999999999] 1597 0 1215");
}
