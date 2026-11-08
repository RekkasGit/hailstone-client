// The user interfaces (ui/ui_packs.h): the folders with a ui.ini, their fields, and which one ui.name picks.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "ui/ui_packs.h"
#include "scratch_dir.h"

namespace fs = std::filesystem;
using namespace hailstone;

TEST_CASE("UI packs: folders with a ui.ini, chosen by name with modern as the fallback") {
    hailstone::test::ScratchDir dir_scratch("ui_packs_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "modern");
    fs::create_directories(dir / "pad");
    fs::create_directories(dir / "not_a_ui");
    std::ofstream(dir / "modern" / "ui.ini") << "# comment\nname=Modern\ndescription=New\ninput=mouse\ngame_art=icons\n";
    std::ofstream(dir / "pad" / "ui.ini") << "name=Gamepad\ninput=gamepad,touch\ngame_art=all\n";
    std::ofstream(dir / "not_a_ui" / "readme.txt") << "x";

    auto packs = ui::list_ui_packs(dir);
    REQUIRE(packs.size() == 2);
    CHECK(packs[1].id == "modern"); // by name: Gamepad, Modern
    CHECK(packs[1].description == "New");
    CHECK_FALSE(packs[1].game_art);
    CHECK(packs[0].name == "Gamepad");
    CHECK(packs[0].input == "gamepad,touch");
    CHECK(packs[0].game_art);

    CHECK(ui::find_ui_pack(packs, "pad")->id == "pad");
    CHECK(ui::find_ui_pack(packs, "missing")->id == "modern");
    CHECK(ui::find_ui_pack({}, "modern") == nullptr);
    CHECK(ui::list_ui_packs(dir / "nowhere").empty());
    // the shipped UIs: modern and gamepad (the Classic UI is gone; a name of it gets the default)
    auto shipped = ui::list_ui_packs(fs::path(__FILE__).parent_path().parent_path() / "ui");
    CHECK(ui::find_ui_pack(shipped, "modern")->id == "modern");
    CHECK(ui::find_ui_pack(shipped, "classic")->id == "modern");
    fs::remove_all(dir);
}
