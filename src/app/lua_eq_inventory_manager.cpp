// The Advanced Inventory (Hailstone's own window, InventoryManagerWnd; ui/<name>/inventorymanager.lua): every item
// the character has, worn, carried and banked, in one list, and the Keep marks (game/kept_items.h).
//   list = eq.AllItems()         -- { { item, place, where, slot, sub, bag, kept }, ... } in place order: worn, carried
//                                   (the general slots, each bag after its slot), bank, shared bank. item: eq.Item's
//                                   table and value (copper), noTrade, temporary, attuned, reqLevel, recLevel,
//                                   equipSlots (bit n: possessions slot n), tradeskill, augment, usable (the
//                                   character may equip or use it: CharacterZoneClient::CanUseItem(item, 1, 0));
//                                   place: "i:<slot>[:<sub>]" (worn and carried), "b:..." (bank), "s:..." (shared
//                                   bank); where: "worn", "carried", "bank", "shared"; bag: the bag's name, for an
//                                   item in one; kept: marked Keep (or a bag holding a kept item)
//   inv, keep = eq.ItemsRevision()  -- change counters: the inventory's and the Keep marks' (a window's cache)
//   on = eq.ItemKept(id)
//   eq.SetItemKept(id, name, on)    -- marks or unmarks every copy of the item (kept per character)
// The bank's and shared bank's items are known from zoning in, so they are listed while the bank is closed; acting on
// them still needs the bank open (eq.BankSlotClick).

#include <format>

#include <lua.hpp>

#include "app/lua_eq.h"
#include "app/lua_eq_windows.h"
#include "net/game_connection.h"

namespace hailstone::app {

namespace {

EqHost& host(lua_State* L) { return *static_cast<EqHost*>(lua_touserdata(L, lua_upvalueindex(1))); }

constexpr int kSharedBankSlots = 2; // BW_SharedBankSlot0..1 (game/bank.h)

void set_int(lua_State* L, const char* k, lua_Integer v) {
    lua_pushinteger(L, v);
    lua_setfield(L, -2, k);
}
void set_bool(lua_State* L, const char* k, bool v) {
    lua_pushboolean(L, v);
    lua_setfield(L, -2, k);
}

// eq.Item's table and what the window filters and sorts by.
void push_full_item(lua_State* L, const net::GameConnection& c, const net::Item& it) {
    push_item(L, &it);
    set_int(L, "value", it.value);
    set_bool(L, "noTrade", it.no_trade());
    set_bool(L, "temporary", it.no_rent == 0);
    set_bool(L, "attuned", it.attuned != 0);
    set_int(L, "reqLevel", it.req_level);
    set_int(L, "recLevel", it.rec_level);
    set_int(L, "equipSlots", it.equip_slots);
    set_bool(L, "tradeskill", it.tradeskills != 0);
    set_bool(L, "augment", it.aug_type != 0 && it.item_type == 54); // ItemType 54: augmentation
    set_bool(L, "usable", c.item_usable(it));
}

int AllItems(lua_State* L) {
    auto& h = host(L);
    lua_newtable(L);
    if (!h.conn || !h.in_game) return 1;
    const net::GameConnection& c = *h.conn;
    const auto& inv = c.inventory();
    lua_Integer n = 0;
    auto row = [&](const net::Item& it, const net::ItemSlot& at, const char* where, const net::Item* bag) {
        lua_createtable(L, 0, 7);
        push_full_item(L, c, it);
        lua_setfield(L, -2, "item");
        const char kind = at.type == net::kInvBank ? 'b' : at.type == net::kInvSharedBank ? 's' : 'i';
        std::string place = std::format("{}:{}", kind, at.slot) + (at.sub >= 0 ? std::format(":{}", at.sub) : std::string());
        lua_pushstring(L, place.c_str());
        lua_setfield(L, -2, "place");
        lua_pushstring(L, where);
        lua_setfield(L, -2, "where");
        set_int(L, "slot", at.slot);
        set_int(L, "sub", at.sub);
        if (bag) {
            lua_pushstring(L, bag->name.c_str());
            lua_setfield(L, -2, "bag");
        }
        set_bool(L, "kept", c.item_kept(it));
        lua_rawseti(L, -2, ++n);
    };
    auto scan = [&](std::int16_t type, int first, int last, const char* where, bool bags) {
        for (int s = first; s <= last; ++s) {
            const net::ItemSlot top{type, static_cast<std::int16_t>(s), -1, -1, 0, 0};
            const net::Item* it = inv.at(top);
            if (!it) continue;
            row(*it, top, where, nullptr);
            if (!bags || !it->is_bag()) continue;
            for (int i = 0; i < it->bag_slots; ++i) {
                net::ItemSlot in = top;
                in.sub = static_cast<std::int16_t>(i);
                if (const net::Item* ci = inv.at(in)) row(*ci, in, where, it);
            }
        }
    };
    scan(net::kInvPossessions, net::kSlotCharm, net::kSlotAmmo, "worn", false);
    scan(net::kInvPossessions, net::kSlotGeneral1, net::kSlotGeneral10, "carried", true);
    scan(net::kInvBank, 0, (c.bank() ? c.bank()->slots : 24) - 1, "bank", true);
    scan(net::kInvSharedBank, 0, kSharedBankSlots - 1, "shared", true);
    return 1;
}

int ItemsRevision(lua_State* L) {
    auto& h = host(L);
    lua_pushinteger(L, h.conn ? static_cast<lua_Integer>(h.conn->inventory().revision()) : 0);
    lua_pushinteger(L, h.conn ? static_cast<lua_Integer>(h.conn->kept_items_revision()) : 0);
    return 2;
}

int ItemKept(lua_State* L) {
    auto& h = host(L);
    auto id = static_cast<std::uint32_t>(luaL_checkinteger(L, 1));
    lua_pushboolean(L, h.conn && h.conn->kept_items().kept(id));
    return 1;
}

int SetItemKept(lua_State* L) {
    auto& h = host(L);
    auto id = static_cast<std::uint32_t>(luaL_checkinteger(L, 1));
    const char* name = luaL_optstring(L, 2, "");
    if (h.conn) h.conn->set_item_kept(id, name, lua_toboolean(L, 3) != 0);
    return 0;
}

class InventoryManagerWindow : public StockWindow {
public:
    InventoryManagerWindow() : StockWindow("InventoryManagerWnd", false) {}
};

} // namespace

void add_inventory_manager_window(std::vector<std::unique_ptr<StockWindow>>& out) {
    out.push_back(std::make_unique<InventoryManagerWindow>());
}

void register_inventory_manager_api(lua_State* L, EqHost& h) {
    static const luaL_Reg fns[] = {{"AllItems", AllItems},
                                   {"ItemsRevision", ItemsRevision},
                                   {"ItemKept", ItemKept},
                                   {"SetItemKept", SetItemKept},
                                   {nullptr, nullptr}};
    lua_pushlightuserdata(L, &h);
    luaL_setfuncs(L, fns, 1);
}

} // namespace hailstone::app
