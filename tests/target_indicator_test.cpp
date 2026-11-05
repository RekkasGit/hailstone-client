// The 3D target indicator's rules (game/target_indicator.h).
#include <doctest/doctest.h>

#include "core/ini_file.h"
#include "game/target_indicator.h"

using namespace nmsc;
using game::Con;

TEST_CASE("target indicator: the con picks the section, con 0 hides the ring") {
    CHECK_FALSE(game::target_ring_section(Con::None));
    CHECK(game::target_ring_section(Con::Gray) == 0);      // Trivial
    CHECK(game::target_ring_section(Con::Green) == 1);     // VeryEasy
    CHECK(game::target_ring_section(Con::LightBlue) == 2); // Easy
    CHECK(game::target_ring_section(Con::DarkBlue) == 3);  // FairlyEasy
    CHECK(game::target_ring_section(Con::White) == 4);     // FairMatch
    CHECK(game::target_ring_section(Con::Yellow) == 5);    // Difficult
    CHECK(game::target_ring_section(Con::Red) == 6);       // Deadly
}

TEST_CASE("target indicator: TargetIndicator.ini's colours, LoadIndicatorSettings' defaults") {
    IniFile ini;
    ini.parse("[TargetIndicator]\nAdditive=1\nPointCount=64\n[Deadly]\nAlpha=255\nRed=255\nGreen=0\nBlue=0\n"
              "FloorOffset=0.5\t\n[Easy]\nRed=0\nGreen=100\nBlue=200\nAlpha=256\n");
    auto s = game::TargetIndicatorSettings::parse(ini);
    const auto& deadly = s.styles[6];
    CHECK(deadly.alpha == 255);
    CHECK(deadly.red == 255);
    CHECK(deadly.green == 0);
    CHECK(deadly.blue == 0);
    CHECK(deadly.floor_offset == doctest::Approx(0.5f));
    CHECK(s.styles[2].green == 100);
    CHECK(s.styles[2].alpha == 0); // kept as a byte
    const auto& trivial = s.styles[0]; // no section: the defaults
    CHECK(trivial.alpha == 0xc0);
    CHECK(trivial.red == 0xff);
    CHECK(trivial.floor_offset == doctest::Approx(1.0f));
}

TEST_CASE("target indicator: the ring's size") {
    CHECK(game::target_ring_radius(3.75f, 1.2f, true) == doctest::Approx(4.5f));
    CHECK(game::target_ring_radius(3.75f, 1.2f, false) == doctest::Approx(3.75f));
    CHECK(game::target_ring_radius(1.0f, 1.0f, true) == doctest::Approx(2.0f));
}
