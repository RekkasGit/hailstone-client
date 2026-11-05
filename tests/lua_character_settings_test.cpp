// The Lua UI's settings per character (src/ui/lua/lua_host.h LuaUi::set_character_settings): the window layout
// keys go to the character's file and fall back to the shared one; the other keys stay shared; the layout copy.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <imgui.h>
#include <lua.hpp>

#include "ui/lua/lua_host.h"
#include "scratch_dir.h"

namespace fs = std::filesystem;

namespace {
std::string run(lua_State* L, const std::string& code) {
    REQUIRE(luaL_dostring(L, code.c_str()) == LUA_OK);
    std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : "nil";
    lua_settop(L, 0);
    return v;
}
std::string read(const fs::path& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
} // namespace

TEST_CASE("Lua UI: the window layout per character, the other settings shared") {
    using nmsc::ui::lua::LuaUi;
    CHECK(LuaUi::character_setting("hud.pos.AAWindow"));
    CHECK(LuaUi::character_setting("hud.scale"));
    CHECK(LuaUi::character_setting("tasks.listw"));
    CHECK_FALSE(LuaUi::character_setting("palette"));
    CHECK_FALSE(LuaUi::character_setting("book.sort"));
    // the item marks are the character's (its file is per server) but not its layout
    CHECK(LuaUi::character_setting("items.Anna.junk"));
    CHECK_FALSE(LuaUi::layout_setting("items.Anna.junk"));
    CHECK(LuaUi::layout_setting("hud.pos.AAWindow"));

    nmsc::test::ScratchDir scratch("lua_character_settings_test");
    const fs::path pack = scratch.path() / "pack", shared = scratch.path() / "shared.txt";
    const fs::path anna = scratch.path() / "anna" / "ui-pack.txt", bob = scratch.path() / "bob" / "ui-pack.txt";
    fs::create_directories(pack);
    std::ofstream(shared) << "hud.pos.AAWindow=10,10\npalette=Midnight\n";

    ImGuiContext* ctx = ImGui::CreateContext();
    {
        LuaUi lua(pack, shared);
        lua_State* L = lua.state();
        // before a character: the shared file
        CHECK(run(L, "return ui.GetSetting('hud.pos.AAWindow')") == "10,10");

        // a character with no layout of its own starts from the shared one; its changes are its own
        lua.set_character_settings(anna);
        CHECK(run(L, "return ui.GetSetting('hud.pos.AAWindow')") == "10,10");
        run(L, "ui.SetSetting('hud.pos.AAWindow', '50,60') ui.SetSetting('palette', 'Ember')");
        CHECK(run(L, "return ui.GetSetting('hud.pos.AAWindow')") == "50,60");

        // another character: the shared layout, the shared palette Anna changed
        lua.set_character_settings(bob);
        CHECK(read(anna) == "hud.pos.AAWindow=50,60\n");
        CHECK(run(L, "return ui.GetSetting('hud.pos.AAWindow')") == "10,10");
        CHECK(run(L, "return ui.GetSetting('palette')") == "Ember");
        run(L, "ui.SetSetting('hud.scale.AAWindow', '1.5')");

        // the layout copy: Anna's layout replaces Bob's; from no character, the shared layout
        lua.copy_character_layout(anna);
        CHECK(read(bob) == "hud.pos.AAWindow=50,60\n");
        CHECK(run(L, "return ui.GetSetting('hud.scale.AAWindow') or 'none'") == "none");
        lua.copy_character_layout({});
        CHECK(run(L, "return ui.GetSetting('hud.pos.AAWindow')") == "10,10");

        lua.set_character_settings(anna);
        CHECK(run(L, "return ui.GetSetting('hud.pos.AAWindow')") == "50,60");
        lua.set_character_settings({});
        CHECK(run(L, "return ui.GetSetting('hud.pos.AAWindow')") == "10,10");
    }
    // the shared file kept the shared layout and took the palette (written when the UI closed)
    CHECK(read(shared) == "hud.pos.AAWindow=10,10\npalette=Ember\n");
    ImGui::DestroyContext(ctx);
}

TEST_CASE("Lua UI: item marks kept per server and character, and a layout copy leaves them") {
    using nmsc::ui::lua::LuaUi;
    nmsc::test::ScratchDir scratch("lua_character_items_test");
    const fs::path pack = scratch.path() / "pack", shared = scratch.path() / "shared.txt";
    // the same name on two servers: one file each (servers/<profile>/<character>/ui-<pack>.txt)
    const fs::path here = scratch.path() / "srvA" / "anna" / "ui-pack.txt";
    const fs::path there = scratch.path() / "srvB" / "anna" / "ui-pack.txt";
    fs::create_directories(pack);
    // marks from before they were per server: read until the character next changes them
    std::ofstream(shared) << "items.Anna.junk=7\n";

    ImGuiContext* ctx = ImGui::CreateContext();
    {
        LuaUi lua(pack, shared);
        lua_State* L = lua.state();
        lua.set_character_settings(here);
        CHECK(run(L, "return ui.GetSetting('items.Anna.junk')") == "7");
        run(L, "ui.SetSetting('items.Anna.junk', '7,9') ui.SetSetting('hud.pos.AAWindow', '1,2')");

        lua.set_character_settings(there);
        CHECK(read(here) == "hud.pos.AAWindow=1,2\nitems.Anna.junk=7,9\n");
        CHECK(run(L, "return ui.GetSetting('items.Anna.junk')") == "7"); // not the other server's marks
        run(L, "ui.SetSetting('items.Anna.junk', '3')");

        // copying the other character's layout brings its window keys, not its marks, and keeps this one's
        lua.copy_character_layout(here);
        CHECK(read(there) == "hud.pos.AAWindow=1,2\nitems.Anna.junk=3\n");
        lua.set_character_settings({});
    }
    CHECK(read(shared) == "items.Anna.junk=7\n");
    ImGui::DestroyContext(ctx);
}
