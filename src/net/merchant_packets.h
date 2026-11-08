#pragma once

// RoF2 merchant packets. None of them has a RoF2 translator except the two
// purchase structs (ENCODE/DECODE(OP_ShopPlayerBuy), (OP_ShopPlayerSell) in
// the NMS server's common/patches/rof2.cpp, which copy the fields one by
// one), so the layouts are rof2_structs.h's (#pragma pack(1)). What the
// original client fills in comes from rof2decomp's export of eqgame.exe:
//
//   OP_ShopRequest  C→Z  CEverQuest::RightClickedOnPlayer @0x536d80 (class 41 = merchant)
//   OP_ShopRequest  Z→C  Handle_OP_ShopRequest @0x4b75f0 → CMerchantWnd::Activate
//   OP_ShopPlayerBuy C→Z CMerchantWnd::PurchasePageHandler::RequestGetItem @0x6f5be0
//   OP_ShopPlayerBuy Z→C Handle_OP_ShopPlayerBuy @0x4b7660 → CMerchantWnd::FinishBuyingItem @0x6ef220
//   OP_ShopPlayerSell C→Z CMerchantWnd::PurchasePageHandler::RequestPutItem @0x6f6650
//   OP_ShopPlayerSell Z→C Handle_OP_ShopPlayerSell @0x4b47b0 → CMerchantWnd::FinishSellingItem @0x6f4200
//   OP_ShopEnd      C→Z  CMerchantWnd::AboutToHide @0x6efe60
//   OP_ShopEnd / OP_ShopEndConfirm Z→C  HandleWorldMessage: the window closes
//   OP_ShopDelItem  Z→C  Handle_OP_ShopDelItem @0x4b7970
//
// The merchant's stock arrives as item packets of type 0x64 (net/item_packets.h).
// Per-packet notes are in docs/protocol/rof2-packets.md section "Merchants".

#include <cstdint>
#include <optional>

#include "net/item_packets.h"
#include "net/packet.h"

namespace hailstone::net {

// MerchantClick_Struct (24 bytes, both directions). The client fills only
// the two ids (the rest of the original's buffer is stack garbage; we send
// command 1, rate 0, tabs 0 and -1 as EQEmu's comment says the client does).
// The server answers with command 1 (open) or 0 (refused), the price rate
// (1 / (BuyCostMod × CalcPriceMod), stored as CMerchantWnd::MerchantGreed and
// used for sell prices) and the tabs to show (1 buy/sell, 2 recover, 4 parcels).
struct MerchantClick {
    std::uint32_t npc_id = 0;
    std::uint32_t player_id = 0;
    std::uint32_t command = 1;
    float rate = 0.0f;
    std::int32_t tab_display = 0;
    std::int32_t unknown02 = -1;
};
inline constexpr std::size_t kMerchantClickSize = 24;
AppPacket encode(const MerchantClick& v); // OP_ShopRequest
std::optional<MerchantClick> decode_merchant_click(const AppPacket& p);

// OP_ShopPlayerBuy (32 bytes, Merchant_Sell_Struct: the merchant "sells").
// Client: merchant id, own id, the item's merchant slot (the header's
// instance id, which the server sets to the merchant slot), the high dword
// of that 64-bit serial (0), the quantity; the original leaves +20..+31
// unset and the server ignores them (we send 0). Server: the same ids and
// slot, the quantity bought and the total price in copper at +24 (the
// original reads +24 as a 64-bit price and +8 as a 64-bit success flag).
struct MerchantBuy {
    std::uint32_t npc_id = 0;
    std::uint32_t player_id = 0;
    std::uint32_t item_slot = 0;
    std::uint32_t unknown12 = 0;
    std::uint32_t quantity = 0;
    std::uint32_t unknown20 = 0;
    std::uint32_t price = 0;
    std::uint32_t unknown28 = 0;
};
inline constexpr std::size_t kMerchantBuySize = 32;
AppPacket encode(const MerchantBuy& v);
std::optional<MerchantBuy> decode_merchant_buy(const AppPacket& p);

// OP_ShopPlayerSell (20 bytes, Merchant_Purchase_Struct: the merchant "buys").
// Client: merchant id, the item's TypelessInventorySlot (slot, bag sub-slot,
// augment, unknown; the original copies the item's slot and sub-slot dword
// and its augment word), quantity; the price dword is not set by the
// original (we send 0, the server computes it). Server: the same, with the
// copper paid; slot -1 means the merchant refused.
struct MerchantSell {
    std::uint32_t npc_id = 0;
    std::int16_t slot = -1;
    std::int16_t sub = -1;
    std::int16_t aug = -1;
    std::int16_t unknown01 = -1;
    std::uint32_t quantity = 0;
    std::uint32_t price = 0;
};
inline constexpr std::size_t kMerchantSellSize = 20;
AppPacket encode(const MerchantSell& v);
std::optional<MerchantSell> decode_merchant_sell(const AppPacket& p);

// OP_ShopEnd (client → zone, 8 bytes): merchant id, own id. The server
// answers OP_ShopEndConfirm (empty).
AppPacket encode_shop_end(std::uint32_t npc_id, std::uint32_t player_id);

// EQEmu's parcels (ParcelRetrieve_Struct, 16 bytes): OP_ShopRetrieveParcel (client → zone) takes a parcel; the
// server answers OP_ShopDeleteParcel with the same struct for one taken, then OP_ShopRetrieveParcel (empty) either
// way. The parcels themselves come as item packets of type 0x73 (ItemPacket's parcel_*) when a parcel merchant opens.
struct ParcelRetrieve {
    std::uint32_t merchant_id = 0;
    std::uint32_t player_id = 0;
    std::uint32_t parcel_slot = 0;
    std::uint32_t item_id = 0;
};
inline constexpr std::size_t kParcelRetrieveSize = 16;
AppPacket encode(const ParcelRetrieve& v);
std::optional<ParcelRetrieve> decode_parcel_retrieve(const AppPacket& p);

// OP_ShopSendParcel (client → zone, 224 bytes, rof2_structs.h Parcel_Struct; MerchantMailPageHandler_RequestPutItem
// @0x6f6a30 sends it): the merchant, the item's TypelessInventorySlot (slot -1 for money), the count (money: the
// copper value of the coin moved to the trade location first), money_flag (0 an item, 1 money), the recipient and
// the note. The server answers with an OP_ShopSendParcel whose slot dword is -1 (an empty one before it when sent).
struct ParcelSend {
    std::uint32_t npc_id = 0;
    std::int16_t slot = -1;
    std::int16_t sub = -1;
    std::uint32_t quantity = 0;
    std::uint32_t money_flag = 0;
    std::string to;
    std::string note;
};
inline constexpr std::size_t kParcelSendSize = 224;
AppPacket encode(const ParcelSend& v);

// OP_ShopDelItem (zone → client, 12 bytes, Merchant_DelItem_Struct): a
// limited item sold out; the merchant slot is at +8.
struct MerchantDelItem {
    std::uint32_t npc_id = 0;
    std::uint32_t player_id = 0;
    std::uint32_t item_slot = 0;
};
inline constexpr std::size_t kMerchantDelItemSize = 12;
AppPacket encode(const MerchantDelItem& v); // server direction, for tests
std::optional<MerchantDelItem> decode_merchant_del_item(const AppPacket& p);

// ---------------------------------------------------------------- alternate currencies (EQEmu's layouts)

// OP_AltCurrency, zone → client: u32 mode, then mode 8 (populate) u32 count, count × 25 bytes {u32 currency, u32 1,
// u32 currency, u32 item id, u32 icon, u32 stack size, u8 shown}; mode 7 (update, 88 bytes) char[64] name, u32
// currency, u32 1, u32 amount, 2 × u32.
struct AltCurrencyEntry {
    std::uint32_t id = 0, item_id = 0, icon = 0, stack_size = 0;
    bool shown = false;
};
struct AltCurrencyUpdate {
    std::uint32_t id = 0, amount = 0;
};
struct AltCurrencyPacket {
    std::vector<AltCurrencyEntry> populate; // mode 8
    std::optional<AltCurrencyUpdate> update; // mode 7
};
inline constexpr std::uint32_t kAltCurrencyUpdate = 7, kAltCurrencyPopulate = 8;
std::optional<AltCurrencyPacket> decode_alt_currency(const AppPacket& p);
AppPacket encode_alt_currency_populate(const std::vector<AltCurrencyEntry>& v); // server direction, for tests
AppPacket encode_alt_currency_update(const AltCurrencyUpdate& v);               // server direction, for tests

// OP_AltCurrencyReclaim (16 bytes, client → zone): u32 currency, u32, u32 count, u32 flag (1: put the cursor's
// currency item in, the count unused; 0: take `count` out as items, onto the cursor).
AppPacket encode_alt_currency_reclaim(std::uint32_t currency, std::uint32_t count, bool put_in);

// A point merchant (NPC class 70). OP_AltCurrencyMerchantRequest {u32 npc}; the answer OP_AltCurrencyMerchantReply
// is text: "<currency>|1|<currency>|<count>" then per item "^<name>|<item id>|<cost>|0|1|<races>|<classes>".
AppPacket encode_alt_currency_merchant_request(std::uint32_t npc);
struct PointMerchantItem {
    std::string name;
    std::uint32_t item_id = 0, cost = 0, races = 0, classes = 0;
};
struct PointMerchantList {
    std::uint32_t currency = 0;
    std::vector<PointMerchantItem> items;
};
std::optional<PointMerchantList> decode_point_merchant_list(const AppPacket& p);
// OP_AltCurrencyPurchase (12 bytes): u32 npc, u32 item id, u32 1.
AppPacket encode_alt_currency_purchase(std::uint32_t npc, std::uint32_t item_id);
// OP_AltCurrencySellSelection (80 bytes, RoF2 AltCurrencySelectItem_Struct: u32 npc, an 8-byte typeless slot, 17 ×
// u32) asks what an item is worth; the answer OP_AltCurrencySellSelection is EQEmu's own
// AltCurrencySelectItemReply_Struct (88 bytes, not translated for RoF2: 10 B, char[64] name, 2 B, u32 cost, 2 × u32).
// OP_AltCurrencySell (20 bytes: u32 npc, the slot, u32 charges, u32 cost) sells it; the server echoes it with the
// charges sold and the price, and deletes the item without telling the client.
AppPacket encode_alt_currency_select(std::uint32_t npc, std::int16_t slot, std::int16_t sub);
struct AltCurrencyQuote {
    std::string item_name;
    std::uint32_t cost = 0;
};
std::optional<AltCurrencyQuote> decode_alt_currency_quote(const AppPacket& p);
AppPacket encode_alt_currency_sell(std::uint32_t npc, std::int16_t slot, std::int16_t sub, std::uint32_t charges,
                                   std::uint32_t cost);
// The sale's echo (OP_AltCurrencySell, 20 bytes, or OP_AdventureMerchantSell, 24 bytes after a u32 1).
struct PointSale {
    std::uint32_t npc = 0;
    std::int16_t slot = -1, sub = -1;
    std::uint32_t charges = 0, price = 0;
};
std::optional<PointSale> decode_alt_currency_sale(const AppPacket& p);
std::optional<PointSale> decode_adventure_merchant_sale(const AppPacket& p);

// Adventure merchants (EQEmu's Handle_OP_AdventureMerchant*): LDoN (class 61, type 1), Discord (59, 2), Norrath's
// Keepers (67, 4) and Dark Reign (68, 5). OP_AdventureMerchantRequest (8 bytes: u32 type, u32 npc); the answer
// OP_AdventureMerchantResponse is u8 count, then per item "^<name>|<item id>|<cost>|<theme>|<stackable>|<lore>|
// <races>|<classes>" (theme: 0 none, 1..5 the LDoN themes). OP_AdventureMerchantPurchase (16 bytes: u32 type, u32
// npc, u32 item id, u32); OP_AdventureMerchantSell (24 bytes: u32 1, u32 npc, the typeless slot, u32 charges, u32
// price; the server computes the price from the item's LDoN price and sell-back rate).
struct AdventureMerchantItem {
    std::string name;
    std::uint32_t item_id = 0, cost = 0, theme = 0, races = 0, classes = 0;
    bool stackable = false, lore = false;
};
AppPacket encode_adventure_merchant_request(std::uint32_t type, std::uint32_t npc);
std::optional<std::vector<AdventureMerchantItem>> decode_adventure_merchant_list(const AppPacket& p);
AppPacket encode_adventure_merchant_purchase(std::uint32_t type, std::uint32_t npc, std::uint32_t item_id);
AppPacket encode_adventure_merchant_sell(std::uint32_t npc, std::int16_t slot, std::int16_t sub, std::uint32_t charges);
// The RoF2 client's sell-back price for one (EQEmu: "matches RoF2 client side calculation"): (price + 1) × rate / 100,
// at most the price; 0 when the merchant will not buy it.
std::uint32_t adventure_sell_price(std::uint32_t ldon_price, std::uint32_t sellback_rate, std::uint32_t ldon_sold);

} // namespace hailstone::net
