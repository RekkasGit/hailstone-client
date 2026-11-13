#include <doctest/doctest.h>

#include <filesystem>
#include <memory>

#include <lua.hpp>

TEST_CASE("Select Reward: both actual UI packs preserve reviewed identities and explicit actions") {
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path();
    const auto script = root / "tests" / "reward_selection_ui_test.lua";
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
