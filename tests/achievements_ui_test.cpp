#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include <lua.hpp>

TEST_CASE("Achievements: both actual UI packs keep requests explicit and preserve received identities") {
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path();
    const auto script = root / "tests" / "achievements_ui_test.lua";
    std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), &lua_close);
    REQUIRE(state);
    luaL_openlibs(state.get());
    const auto source = root.generic_string();
    lua_pushlstring(state.get(), source.data(), source.size());
    lua_setglobal(state.get(), "CLIENT_ROOT");
    const auto path = script.string();
    const int result = luaL_dofile(state.get(), path.c_str());
    REQUIRE_MESSAGE(result == LUA_OK, lua_tostring(state.get(), -1));
}

// Both packs browse with the same lib/achievement_browser.lua: the gamepad pack's is a copy, kept the same.
TEST_CASE("Achievements: the packs' achievement_browser.lua are the same file") {
    const auto ui = std::filesystem::path(__FILE__).parent_path().parent_path() / "ui";
    auto read = [](const std::filesystem::path& p) {
        std::ifstream in(p, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    const std::string modern = read(ui / "modern" / "lib" / "achievement_browser.lua");
    CHECK_FALSE(modern.empty());
    CHECK(modern == read(ui / "gamepad" / "lib" / "achievement_browser.lua"));
}
