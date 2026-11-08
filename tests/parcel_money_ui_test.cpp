// The merchant's Parcels page and the inventory's coin buttons (0x698290 before CInventoryWnd::ClickedMoneyButton's
// quantity dialog; app/lua_eq_items.cpp, GameConnection::parcel_coin_button / set_parcel_money) over the eq API,
// against the mock server.
#include <doctest/doctest.h>

#include <imgui.h>
#include <lua.hpp>

#include "app/lua_eq.h"
#include "mock_server/harness.h"
#include "net/item_packets.h"
#include "net/merchant_packets.h"
#include "net/packet_io.h"

using namespace hailstone;
using namespace hailstone::test;

namespace {

constexpr std::uint32_t kMerchant = 5001;

// A parcel merchant beside the player, opened with all three pages, and 7 platinum 3 gold in the purse, none held.
void open_parcel_merchant(net::GameConnection& conn) {
    auto& w = conn.world_for_test();
    REQUIRE(w.player());
    game::Spawn m = *w.player();
    m.id = kMerchant;
    m.name = "Parcel_Merchant00";
    m.class_id = 41;
    m.is_npc = true;
    w.spawns[kMerchant] = m;
    conn.open_merchant(kMerchant);
    REQUIRE(conn.merchant());
    net::MerchantClick open;
    open.npc_id = kMerchant;
    open.player_id = w.player_id;
    open.rate = 1.0f;
    open.tab_display = 7;
    conn.receive_zone_packet_for_test(net::encode(open));
    REQUIRE(conn.merchant()->open);
    // The mock's profile has coin on the cursor: into the purse first (a coin button with coin held).
    for (int t = 0; t < 4; ++t)
        if (conn.cursor_coins()[static_cast<std::size_t>(3 - t)]) conn.inventory_money_button(t, -1, nullptr);
    REQUIRE(conn.cursor_coins() == std::array<std::uint32_t, 4>{});
    conn.receive_zone_packet_for_test(net::encode_money_update(net::Coins{7, 3, 0, 0}));
    REQUIRE(conn.coins().platinum == 7);
}

struct Lua {
    lua_State* L = luaL_newstate();
    explicit Lua(app::EqApi& api) {
        luaL_openlibs(L);
        lua_newtable(L);
        api.register_functions(L);
        lua_setglobal(L, "eq");
    }
    ~Lua() { lua_close(L); }
    void run(const char* code) {
        REQUIRE(luaL_dostring(L, code) == LUA_OK);
        lua_settop(L, 0);
    }
    bool flag(const char* code) {
        REQUIRE(luaL_dostring(L, code) == LUA_OK);
        bool v = lua_toboolean(L, -1);
        lua_settop(L, 0);
        return v;
    }
    lua_Integer integer(const char* code) {
        REQUIRE(luaL_dostring(L, code) == LUA_OK);
        lua_Integer v = lua_tointeger(L, -1);
        lua_settop(L, 0);
        return v;
    }
};

} // namespace

TEST_CASE("Parcels page: an inventory coin button picks money to send, lets the bag item go, and moves no coin") {
    ImGuiContext* ctx = ImGui::CreateContext(); // the eq API reads the keyboard's modifiers
    ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
    MockConfig config = MockConfig::standard();
    Rig rig(std::move(config));
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    open_parcel_merchant(conn);

    app::EqHost host;
    host.in_game = host.ui_ready = true;
    host.conn = &conn;
    app::EqApi api(host);
    Lua lua(api);

    // The rations picked in the bags for sending, on the Parcels page.
    conn.merchant_select(net::ItemSlot::possessions(23));
    REQUIRE(conn.merchant()->selected);
    lua.run("eq.SetTabPage('MerchantWnd', 'MW_MerchantSubwindows', 2)");
    const auto moves_before = MockEqServer::count(rig.server.zone_log, net::Op::MoveCoin);

    // Platinum: the item goes, the button is outlined, the dialog asks how much (all 7 to start).
    CHECK(lua.flag("return eq.ClickChild('InventoryWindow', 'IW_Money0', 'left')"));
    CHECK_FALSE(conn.merchant()->selected);
    CHECK(conn.merchant()->parcel_coin == 0);
    CHECK(lua.flag("return select(2, eq.ChildState('InventoryWindow', 'IW_Money0')) == true"));
    CHECK(lua.flag("return select(2, eq.ChildState('InventoryWindow', 'IW_Money1')) == false"));
    REQUIRE(host.quantity);
    CHECK(host.quantity->max == 7);
    auto done = std::move(host.quantity->done);
    host.quantity.reset();
    done(4);
    // The answer is the money picked for sending (Hailstone): nothing on the cursor, the purse as it was.
    REQUIRE(conn.merchant()->parcel_money);
    CHECK(conn.merchant()->parcel_money->coin == 0);
    CHECK(conn.merchant()->parcel_money->amount == 4);
    CHECK(conn.cursor_coins()[3] == 0);
    CHECK(conn.coins().platinum == 7);
    CHECK(lua.integer("local _, _, _, _, _, m = eq.Parcels() return m.amount") == 4);
    CHECK(lua.integer("local _, _, _, _, _, m = eq.Parcels() return m.coin") == 0);
    const lua_Integer serial = lua.integer("local _, _, _, _, _, m = eq.Parcels() return m.serial");

    // Copper with none in the purse: SelectSlot(0) still runs, but no outline and no dialog.
    lua.run("eq.ClickChild('InventoryWindow', 'IW_Money3', 'left')");
    CHECK(conn.merchant()->parcel_coin == -1);
    CHECK_FALSE(host.quantity);

    // A bag click (SelectSlot) clears the outline.
    lua.run("eq.ClickChild('InventoryWindow', 'IW_Money1', 'left')");
    CHECK(conn.merchant()->parcel_coin == 1);
    REQUIRE(host.quantity);
    done = std::move(host.quantity->done);
    host.quantity.reset();
    done(2);
    CHECK(lua.integer("local _, _, _, _, _, m = eq.Parcels() return m.serial") != serial);
    conn.merchant_select(net::ItemSlot::possessions(23));
    CHECK(conn.merchant()->parcel_coin == -1);

    // Off the Parcels page the button is the original's: the answer comes onto the cursor (OP_MoveCoin 1 -> 0).
    lua.run("eq.SetTabPage('MerchantWnd', 'MW_MerchantSubwindows', 0)");
    lua.run("eq.ClickChild('InventoryWindow', 'IW_Money0', 'left')");
    CHECK(conn.merchant()->selected); // no SelectSlot(0) here
    REQUIRE(host.quantity);
    done = std::move(host.quantity->done);
    host.quantity.reset();
    done(3);
    CHECK(conn.cursor_coins()[3] == 3);
    CHECK(conn.coins().platinum == 4);
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, net::Op::MoveCoin) > moves_before; }));
    ImGui::DestroyContext(ctx);
}
