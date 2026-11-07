// The Advanced Inventory (ui/modern/inventorymanager.lua over app/lua_eq_inventory_manager.cpp) and the Keep marks
// (game/kept_items.h), against the mock server: the window draws in both views without script errors and lists what
// the character wears and carries; a kept item is not destroyed or dropped; the marks are kept per character.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>
#include <lua.hpp>

#include "app/lua_eq.h"
#include "app/lua_eq_menu.h"
#include "mock_server/harness.h"
#include "ui/lua/lua_host.h"
#include "scratch_dir.h"

using namespace nmsc;
using namespace nmsc::test;
namespace fs = std::filesystem;

namespace {

fs::path ui_dir() { return fs::path(__FILE__).parent_path().parent_path() / "ui"; }

void frame(ui::lua::LuaUi& lua) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280, 800);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    lua.frame();
    ImGui::Render();
}

std::string file_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("Advanced Inventory: lists what is worn and carried, draws in both views, and marks items") {
    fs::path src = ui_dir() / "modern";
    REQUIRE(fs::exists(src / "inventorymanager.lua"));
    ScratchDir scratch("inventory_manager_ui_test");
    fs::path dir = scratch.path();
    fs::copy(src, dir / "modern", fs::copy_options::recursive);

    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        net::ConnectionOptions options = quick_options();
        options.servers_dir = dir / "servers";
        Rig rig(MockConfig::standard(), options);
        REQUIRE(rig.to_zone());
        auto& conn = rig.conn;

        app::EqHost host;
        host.in_game = host.ui_ready = true;
        host.game = &rig.game;
        host.conn = &conn;
        app::EqApi api(host);
        api.load_layout(dir / "defaults.ini", dir / "UI_Test_Server.ini", {});
        ui::lua::LuaUi lua(dir / "modern", dir / "modern.txt");
        lua.add_eq_api([&](lua_State* L) {
            api.register_functions(L);
            lua_pushcfunction(L, [](lua_State* S) { lua_pushboolean(S, 1); return 1; });
            lua_setfield(L, -2, "InGame");
        });
        lua_State* L = lua.state();
        auto frames = [&] {
            for (int i = 0; i < 3; ++i) {
                api.begin_frame();
                frame(lua);
                rig.step();
            }
        };
        api.show("InventoryManagerWnd", true);
        frames();
        CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
        CHECK(ImGui::FindWindowByName("###modern.InventoryManagerWnd") != nullptr);
        // the pack draws it, so the EQ menu offers its row after Inventory
        CHECK(app::menu_command_offered(app::kInventoryManagerCommand, [&](std::string_view s) { return api.replaces(s); }));

        // the mock's character: a Short Sword in Primary, five Rations in the first general slot
        REQUIRE(luaL_dostring(L, R"lua(
local l = eq.AllItems()
return #l, l[1].where, l[1].item.name, l[1].place, l[2].where, l[2].item.count, l[2].place, l[2].kept
)lua") == LUA_OK);
        CHECK(lua_tointeger(L, 1) == 2);
        CHECK(std::string(lua_tostring(L, 2)) == "worn");
        CHECK(std::string(lua_tostring(L, 3)) == "Short Sword");
        CHECK(std::string(lua_tostring(L, 4)) == "i:13");
        CHECK(std::string(lua_tostring(L, 5)) == "carried");
        CHECK(lua_tointeger(L, 6) == 5);
        CHECK(std::string(lua_tostring(L, 7)) == "i:23");
        CHECK_FALSE(lua_toboolean(L, 8));
        lua_settop(L, 0);

        // Keep through the shared tools: the client has it, and a Sell mark on the same item clears it
        REQUIRE(luaL_dostring(L, R"lua(
local tools = dofile("lib/itemtools.lua")
local rations = eq.Item(23)
tools.SetKeep(rations, true)
local kept = tools.Kept(rations)
tools.SetSell(rations, true)
return kept, eq.ItemKept(rations.id), tools.Junk(rations)
)lua") == LUA_OK);
        CHECK(lua_toboolean(L, 1));
        CHECK_FALSE(lua_toboolean(L, 2)); // Sell took the Keep mark off
        CHECK(lua_toboolean(L, 3));
        lua_settop(L, 0);
        REQUIRE(luaL_dostring(L, "local t = dofile('lib/itemtools.lua') t.SetKeep(eq.Item(23), true)") == LUA_OK);
        CHECK(conn.kept_items().kept(1002));
        frames();
        CHECK(ImGui::FindWindowByName("UI errors") == nullptr);

        // the grid, a filter and a sort
        REQUIRE(luaL_dostring(L, "ui.SetSetting('advinv.view', 'grid') ui.SetSetting('advinv.sort', 'Kind') "
                                 "ui.SetSetting('advinv.mark', 'keep')") == LUA_OK);
        frames();
        CHECK(ImGui::FindWindowByName("UI errors") == nullptr);

        // the inventory window, with its Advanced Inventory button, draws too
        api.show("InventoryWindow", true);
        frames();
        CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
        CHECK(ImGui::FindWindowByName("###modern.InventoryWindow") != nullptr);
    }
    ImGui::DestroyContext(ctx);
}

TEST_CASE("Kept items: not destroyed or dropped, and kept per character in kept_items.txt") {
    ScratchDir scratch("kept_items_conn_test");
    net::ConnectionOptions options = quick_options();
    options.servers_dir = scratch.path();
    {
        Rig rig(MockConfig::standard(), options);
        REQUIRE(rig.to_zone());
        auto& conn = rig.conn;
        conn.set_item_kept(1002, "Rations", true);
        fs::path file = conn.own_files().kept_items();
        REQUIRE(fs::exists(file));
        CHECK(file_text(file).find("\n1002 Rations\n") != std::string::npos);

        // the whole stack onto the cursor
        conn.click_slot(net::ItemSlot::possessions(net::kSlotGeneral1), true);
        REQUIRE(conn.inventory().cursor());
        CHECK(conn.inventory().cursor()->id == 1002u);
        conn.destroy_held();
        conn.destroy_cursor();
        CHECK(conn.inventory().cursor());       // still held
        CHECK_FALSE(conn.request_drop_cursor()); // no drop confirmation asked for
        CHECK_FALSE(conn.drop_cursor_on_ground(true));
        CHECK(conn.inventory().cursor());

        // unmarked, it goes
        conn.set_item_kept(1002, "", false);
        conn.destroy_cursor();
        CHECK_FALSE(conn.inventory().cursor());
        conn.set_item_kept(1001, "Short Sword", true);
    }
    // the next login of the character reads them back
    Rig again(MockConfig::standard(), options);
    REQUIRE(again.to_zone());
    CHECK(again.conn.kept_items().kept(1001));
    CHECK_FALSE(again.conn.kept_items().kept(1002));
}
