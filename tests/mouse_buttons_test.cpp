// Mouse buttons beyond left and right as bindable keys: platform::dik_from_sdl_mouse_button gives the original's
// codes 0xf0..0xf5 (FUN_0051a770), and the key map binds, names, presses and saves them like any key.
#include <doctest/doctest.h>

#include <vector>

#include "core/config.h"
#include "game/keymap.h"
#include "platform/window.h"

using namespace nmsc;
using namespace nmsc::game;

TEST_CASE("Extra mouse buttons map to the original's MOUS codes") {
    // SDL: 1 left, 2 middle, 3 right, 4 X1, 5 X2, then the buttons past X2.
    CHECK(platform::dik_from_sdl_mouse_button(1) == 0);
    CHECK(platform::dik_from_sdl_mouse_button(3) == 0);
    CHECK(platform::dik_from_sdl_mouse_button(2) == 0xf0);
    CHECK(platform::dik_from_sdl_mouse_button(4) == 0xf1);
    CHECK(platform::dik_from_sdl_mouse_button(5) == 0xf2);
    CHECK(platform::dik_from_sdl_mouse_button(6) == 0xf3);
    CHECK(platform::dik_from_sdl_mouse_button(7) == 0xf4);
    CHECK(platform::dik_from_sdl_mouse_button(8) == 0xf5);
    CHECK(platform::dik_from_sdl_mouse_button(9) == 0); // DirectInput's DIMOUSESTATE2 has eight buttons
    CHECK(platform::dik_from_sdl_mouse_button(0) == 0);

    CHECK(key_text(KeyCombo{0xf0}) == "MOUS MID");
    CHECK(key_text(KeyCombo{0xf1}) == "MOUS AUX1");
    CHECK(key_text(KeyCombo{0xf5}) == "MOUS AUX5");
    CHECK(key_text(KeyCombo{0xf2, true, false, false}) == "Shift MOUS AUX2");
    CHECK_FALSE(is_reserved_key(KeyCombo{0xf1}));
}

TEST_CASE("A mouse button binds, runs its command with its modifiers and survives a save") {
    KeyMap km;
    const int hot = static_cast<int>(KeyCommand::HOT1_1);
    const int consider = static_cast<int>(KeyCommand::CONSIDER);
    REQUIRE(km.bind(hot, 0, KeyCombo{0xf1}));
    REQUIRE(km.bind(consider, 1, KeyCombo{0xf1, true, false, false}));

    std::vector<KeyMap::Event> ev;
    km.key_down(KeyCombo{0xf1}, ev);
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].command == hot);
    CHECK(ev[0].down);
    CHECK(km.held(hot));
    ev.clear();
    km.key_up(0xf1, ev);
    REQUIRE(ev.size() == 1);
    CHECK_FALSE(ev[0].down);
    CHECK_FALSE(km.held(hot));

    ev.clear();
    km.key_down(KeyCombo{0xf1, true, false, false}, ev); // Shift + the button is its own binding
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].command == consider);
    km.key_up(0xf1, ev);

    Config cfg;
    km.save(cfg);
    CHECK(cfg.get_int("KEYMAPPING_HOT1_1_1", 0) == 0xf1);
    CHECK(cfg.get_int("KEYMAPPING_CONSIDER_2", 0) == 0x400000f1);
    KeyMap back;
    back.load(cfg);
    CHECK(back.binding(hot, 0) == KeyCombo{0xf1});
    CHECK(back.binding(consider, 1) == KeyCombo{0xf1, true, false, false});
}
