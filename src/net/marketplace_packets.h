#pragma once

// The marketplace (CMarketplaceWnd, the in-game store): its catalog, funding tiers, per-row states, wallet and
// purchases. None of these opcodes is in EQEmu's patch_RoF2.conf (stock EQEmu has no marketplace); a server that
// sells through the window answers the original client's opcodes, and so does this client. Layouts follow the
// original's readers and senders (CEverQuest::HandleWorldMessage's cases 0x1e9f @0x4d2969, 0x09bf @0x4d2aef, 0x4b7f
// @0x4d2b3e, 0x77a7 @0x4d2c73, 0x0a37 @0x4d2ee9, 0x0114 @0x4d30ef; MarketplaceItem_Deserialize @0x7f8a30,
// MarketplaceFund_Deserialize @0x7f9170, CMarketplaceWnd::WndNotification @0x6e1f70, AboutToShow @0x6e35d0) as the
// original reads and sends them. Per-packet notes: docs/protocol/rof2-packets.md
// "Marketplace".
//
// The catalog, funding and state feeds are read the way the original's CUnSerializeBuffer reads them: every field
// on its own guard, so a short packet leaves the fields it lacks at their defaults instead of failing (a later u8
// can still be read where a u32 no longer fits). Where the original reads past the packet (an unterminated string,
// a string length longer than the packet) these decoders stop at its end.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "net/packet.h"

namespace hailstone::net {

// C->S.
inline constexpr std::uint16_t kOpMarketplaceRequestItems = 0x5148;   // 2 bytes: the catalog (list empty on open)
inline constexpr std::uint16_t kOpMarketplaceRequestFunding = 0x76c3; // 2 bytes: the funding tiers (list empty)
inline constexpr std::uint16_t kOpMarketplaceReady = 0x356f;          // 2 bytes: every open, and after a catalog
inline constexpr std::uint16_t kOpMarketplaceHello = 0x5bd5;          // 78 bytes, at every open (Send @0x6d1ab0)
inline constexpr std::uint16_t kOpMarketplacePurchase = 0x4a74;       // 82 bytes: entry id +0x48, quantity +0x4c
// S->C.
inline constexpr std::uint16_t kOpMarketplaceItems = 0x1e9f;          // the catalog, a full replace
inline constexpr std::uint16_t kOpMarketplaceFunding = 0x09bf;        // the funding tiers, a full replace
inline constexpr std::uint16_t kOpMarketplaceStates = 0x77a7;         // the rows' states (answer to Ready)
inline constexpr std::uint16_t kOpMarketplaceWallet = 0x4b7f;         // the balance
inline constexpr std::uint16_t kOpMarketplacePurchaseResult = 0x0a37; // a purchase's answer
inline constexpr std::uint16_t kOpMarketplaceUnavailable = 0x0114;    // eqstr 7631 and the window closed
inline constexpr std::uint16_t kOpMarketplacePreview = 0xb07f;        // OP_ItemPreviewRequest's reply for the window

// ---------------------------------------------------------------- the catalog (0x1e9f)

// One listing (the 0x2118-byte MarketplaceItem node, ctor @0x7f8970 for the defaults). Strings are NUL-terminated
// on the wire and cut to the node's buffer (name 127 characters, the 64-byte one 63, the others 1023).
struct MarketplaceEntry {
    std::uint32_t id = 0;            // +0x10: the key purchases and the state feed name
    std::uint32_t price = 0;         // +0x8d4: what one costs now
    std::uint32_t icon = 0;          // +0x8d8: items.icon (drawn as icon - 500; 0 draws the blank cell)
    std::uint32_t max_quantity = 0;  // +0x8dc: per purchase; 0 none (the client also stops at 10)
    std::string name;                // +0x454
    std::string category;            // +0x4d4: "A/B/C", the category tree's only source
    std::string description;         // +0x54: STML
    std::string text14;              // +0x14 (64): no reader known
    std::uint8_t flag8e0 = 0;        // +0x8e0: no reader known
    bool is_new = false;             // +0x8e1: the New panel and the "*" mark
    std::string text8e2;             // +0x8e2: no reader known
    std::string purchase_message;    // +0xce2: the start of the purchase result's text (HWM case 0x0a37)
    bool item_of_week = false;       // +0x10e2
    std::int32_t item_of_week_rank = -1; // +0x10e4: the highest wins (ctor -1)
    std::uint32_t kind = 0;          // +0x10e8: 0 in-game; 3 quest-reward gate; 4 and 6 Station Cash (dead pages)
    std::uint32_t u10ec = 0;         // +0x10ec
    std::string text10f0, text14f0, text18f0, text1cf0; // +0x10f0..+0x1cf0: kind 4 compares the first two
    std::uint32_t u20f0 = 0, u20f4 = 0;
    bool is_featured = false;        // +0x20f8: the Featured panel
    std::uint32_t was_price = 0;     // +0x20fc: struck through when price < was_price
    // +0x2100 / +0x2104: relative seconds on the wire, kept as an absolute time (time(NULL) + value; 0 on the
    // wire is -1, never). A packet that ends before one of them stores the read's own time() value instead
    // (the original reads into the variable time() just wrote), as it does for +0x2108.
    std::int64_t available_until = -1;
    std::int64_t sale_until = -1;
    std::uint32_t u2108 = 0;
    std::uint32_t money_type = 0;    // +0x210c: 0 the integer price, 1 "%1.2f" of price * 0.01, else "N/A"
    std::uint32_t u2110 = 0;
    std::uint8_t u2114 = 0;
};
// `now` is time(NULL) when the packet arrived. Entries are kept in wire order. A count the packet cannot hold stops
// at the packet's end (the original goes on making default entries, 0x2118 bytes each, for the whole count).
std::optional<std::vector<MarketplaceEntry>> decode_marketplace_items(const AppPacket& p, std::int64_t now = 0);
AppPacket encode_marketplace_items(const std::vector<MarketplaceEntry>& v, std::int64_t now = 0); // server direction

// ---------------------------------------------------------------- funding tiers (0x09bf)

// Station Cash amounts for Add Funds (the 0x94-byte node). The client only shows them on the funding pages, whose
// purchase went to the publisher's web service.
struct MarketplaceFund {
    std::uint32_t amount = 0;   // +0x10
    std::uint32_t u0c = 0;      // +0x0c
    std::string text14, text54; // 63 characters each; the window keys its rows on text54
};
std::optional<std::vector<MarketplaceFund>> decode_marketplace_funding(const AppPacket& p);
AppPacket encode_marketplace_funding(const std::vector<MarketplaceFund>& v); // server direction

// ---------------------------------------------------------------- row states (0x77a7)

// u32 count, then {u32 entry id, u32 state, u32 item id, u32, u32 length + bytes}. A field the packet lacks keeps
// the previous entry's value (the handler's locals are set once, before its loop); a missing length reads as 0.
struct MarketplaceRowState {
    std::uint32_t entry_id = 0;
    std::uint32_t state = 0;    // 2: not counted (every row starts at 2)
    std::uint32_t item_id = 0;  // the preview's item; 0 none
    std::uint32_t u10 = 0;      // row +0x10
    std::string category;       // a second category for the row; empty none
};
std::optional<std::vector<MarketplaceRowState>> decode_marketplace_states(const AppPacket& p);
AppPacket encode_marketplace_states(const std::vector<MarketplaceRowState>& v); // server direction

// ---------------------------------------------------------------- the wallet (0x4b7f)

// Read as a struct at fixed offsets (no reader): +0x44 status (0, or only a debug line is written), +0x48 the
// balance, +0x4c a byte (the "billing zip on file" flag: 1 skips the zip page), +0x50 the type (1 the marketplace's;
// 2 the Krono count, not kept here). The original reads 0x54 bytes whatever the size; a shorter packet is refused.
struct MarketplaceWallet {
    std::uint32_t status = 0;
    std::uint32_t balance = 0;
    bool zip_on_file = false;
    std::uint32_t type = 1;
};
inline constexpr std::size_t kMarketplaceWalletSize = 0x54;
std::optional<MarketplaceWallet> decode_marketplace_wallet(const AppPacket& p);
AppPacket encode(const MarketplaceWallet& v); // server direction

// ---------------------------------------------------------------- the purchase (0x4a74) and its result (0x0a37)

// 80 bytes: zeros, the entry id at +0x48 and the quantity at +0x4c (the original leaves +0x40..+0x47 as stack
// residue; this client sends zeros).
inline constexpr std::size_t kMarketplacePurchaseSize = 80;
AppPacket encode_marketplace_purchase(std::uint32_t entry_id, std::uint32_t quantity);

// Read as a struct at fixed offsets: +0x40 the entry id, +0xc8 a byte (set: success, result code 0), +0xd8 a dword
// (set: the Station Cash success line, eqstr 7621, else 5361), +0xdc the new balance, +0xe0 where the item went
// (1 inventory, 2 the bank, 4 the shared bank), +0xec the failure code (when +0xc8 is clear), and two strings at
// +0xf0 and +0x4f0 appended to the success text. The original reads them whatever the size; bytes the packet
// lacks read as zero here.
struct MarketplacePurchaseResult {
    std::uint32_t entry_id = 0;
    bool success = false;
    bool station_cash = false;
    std::uint32_t balance = 0;
    std::uint32_t placed = 0;
    std::uint32_t code = 0;
    std::string note_f0, note_4f0;
};
inline constexpr std::size_t kMarketplacePurchaseResultSize = 0x8f0; // the two strings at 1024 bytes each
std::optional<MarketplacePurchaseResult> decode_marketplace_purchase_result(const AppPacket& p);
AppPacket encode(const MarketplacePurchaseResult& v); // server direction

// ---------------------------------------------------------------- the small requests

// 0x5148 / 0x76c3 / 0x356f: the opcode alone.
AppPacket encode_marketplace_request(std::uint16_t opcode);
// 0x5bd5 (Send @0x6d1ab0): 76 bytes, zero but the dword +0x48 = 1.
inline constexpr std::size_t kMarketplaceHelloSize = 76;
AppPacket encode_marketplace_hello();
// OP_ItemPreviewRequest from the window (CMarketplaceWnd_SendItemPreview @0x6d86c0): 80 bytes, the item id, empty
// ornament names, -1 at +0x40, and the u16 +0x4c = 1, which a server answers on 0xb07f (the object preview window
// sends 0 there and is answered on 0x7f80).
AppPacket encode_marketplace_preview_request(std::uint32_t item_id);

} // namespace hailstone::net
