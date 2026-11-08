// The inventory's Alt. Currency page over the eq API (app/lua_eq_items.cpp InventoryWindow, app/lua_eq.cpp
// AltCurrencies): a UI pack that draws the page itself selects a currency with eq.ListClick and claims or reclaims
// it through the stock buttons, without the stock list ever being drawn.
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include <imgui.h>
#include <lua.hpp>

#include "app/lua_eq.h"
#include "app/lua_eq_windows.h"
#include "core/game_dir.h"
#include "net/game_connection.h"
#include "net/merchant_packets.h"
#include "scratch_dir.h"

using namespace hailstone;

namespace {

std::string run(lua_State* L, const std::string& code) {
    if (luaL_dostring(L, code.c_str()) != LUA_OK) {
        std::string e = lua_tostring(L, -1);
        lua_settop(L, 0);
        return "error: " + e;
    }
    std::string out = lua_isnil(L, -1) ? "nil" : luaL_tolstring(L, -1, nullptr);
    lua_settop(L, 0);
    return out;
}

} // namespace

TEST_CASE("Alt. currency page: eq.ListClick selects by eq.AltCurrencies' order, the list never drawn") {
    ImGuiContext* ctx = ImGui::CreateContext();
    {
        hailstone::test::ScratchDir scratch("alt_currency_ui");
        GameDir game(scratch.path());
        net::GameConnection conn(game, {});
        app::EqHost host;
        host.in_game = host.ui_ready = true;
        host.conn = &conn;
        app::EqApi api(host);
        lua_State* L = luaL_newstate();
        luaL_openlibs(L);
        lua_newtable(L);
        api.register_functions(L);
        lua_setglobal(L, "eq");

        // 31 held, 32 none of, 33 held: without Display Missing the rows are 31, 33
        std::vector<net::AltCurrencyEntry> entries{
            {31, 22000, 1500, 1000, true}, {32, 22001, 1501, 500, true}, {33, 22002, 1502, 20, true}};
        conn.receive_zone_packet_for_test(net::encode_alt_currency_populate(entries));
        conn.receive_zone_packet_for_test(net::encode_alt_currency_update({31, 7}));
        conn.receive_zone_packet_for_test(net::encode_alt_currency_update({33, 3}));
        CHECK(run(L, "return #eq.AltCurrencies()") == "2");

        const std::string inv = "'InventoryWindow'", list = "'AltCurr_PointList'";
        CHECK(run(L, "return (eq.ChildState(" + inv + ", 'AltCurr_CreateItemButton'))") == "false"); // none picked
        CHECK(run(L, "return eq.ListClick(" + inv + ", " + list + ", 1, 'left')") == "true");
        CHECK(run(L, "return (select(3, eq.ListInfo(" + inv + ", " + list + ")))") == "1");
        CHECK(run(L, "return (eq.WindowText(" + inv + ", 'AltCurr_MaxStackLabel'))") == "Max stack: 20"); // 33
        CHECK(run(L, "return (eq.ChildState(" + inv + ", 'AltCurr_CreateItemButton'))") == "true");
        CHECK(run(L, "return (eq.ChildState(" + inv + ", 'AltCurr_ReclaimButton'))") == "false"); // nothing held

        // Display Missing: row 1 is now 32, as eq.AltCurrencies(true) lists it
        CHECK(run(L, "return eq.ClickChild(" + inv + ", 'AltCurr_DisplayMissingButton', 'left')") == "true");
        CHECK(run(L, "return (select(2, eq.ChildState(" + inv + ", 'AltCurr_DisplayMissingButton')))") == "true");
        CHECK(run(L, "return eq.AltCurrencies(true)[2].maxStack") == "500");
        CHECK(run(L, "return eq.ListClick(" + inv + ", " + list + ", 1, 'left')") == "true");
        CHECK(run(L, "return (eq.WindowText(" + inv + ", 'AltCurr_MaxStackLabel'))") == "Max stack: 500");
        CHECK(run(L, "return (eq.ChildState(" + inv + ", 'AltCurr_CreateItemButton'))") == "false"); // none of it
        lua_close(L);
    }
    ImGui::DestroyContext(ctx);
}
