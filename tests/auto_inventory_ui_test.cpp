// The inventory and bank windows' Auto-Inventory button (Hailstone_AutoInventory, app/lua_eq_items.cpp; the modern
// pack's inventory.lua and bank.lua draw it) over the eq API, against the mock server.
#include <doctest/doctest.h>

#include <imgui.h>
#include <lua.hpp>

#include "app/lua_eq.h"
#include "mock_server/harness.h"
#include "net/item_packets.h"
#include "net/packet_io.h"

using namespace nmsc;
using namespace nmsc::test;

TEST_CASE("Auto-Inventory button: enabled with an item on the cursor; a click puts it in the inventory") {
    ImGuiContext* ctx = ImGui::CreateContext(); // the eq API reads the keyboard's modifiers
    net::Item held;
    held.id = 13005;
    held.name = "Iron Ration";
    held.slot = net::ItemSlot::possessions(net::kSlotCursor);
    MockConfig config = MockConfig::standard();
    config.characters[0].inventory.push_back(held);
    Rig rig(std::move(config));
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    REQUIRE(conn.inventory().cursor());

    app::EqHost host;
    host.in_game = host.ui_ready = true;
    host.conn = &conn;
    app::EqApi api(host);
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    lua_newtable(L);
    api.register_functions(L);
    lua_setglobal(L, "eq");
    auto enabled = [&] {
        REQUIRE(luaL_dostring(L, "return (eq.ChildState('InventoryWindow', 'Hailstone_AutoInventory'))") == LUA_OK);
        bool on = lua_toboolean(L, -1);
        lua_settop(L, 0);
        return on;
    };
    CHECK(enabled());
    REQUIRE(luaL_dostring(L, "return eq.ClickChild('InventoryWindow', 'Hailstone_AutoInventory', 'left')") == LUA_OK);
    CHECK(lua_toboolean(L, -1));
    lua_settop(L, 0);
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, net::Op::MoveItem) >= 1; }));
    CHECK_FALSE(conn.inventory().cursor());
    const net::AppPacket* move = nullptr;
    for (const auto& p : rig.server.zone_log)
        if (net::is_op(p, net::Op::MoveItem)) move = &p;
    REQUIRE(move);
    auto mv = net::decode_move_item(*move);
    REQUIRE(mv);
    CHECK(mv->from.slot == net::kSlotCursor);
    CHECK(mv->to.slot >= net::kSlotGeneral1);
    // Nothing on the cursor: the button is off.
    CHECK_FALSE(enabled());
    lua_close(L);
    ImGui::DestroyContext(ctx);
}
