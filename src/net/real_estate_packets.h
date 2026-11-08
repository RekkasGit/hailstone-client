#pragma once

// Real estate (player housing, guild halls, placed furniture): the packets RealEstateManagerClient_HandleMessage
// @0x5b2500 reads and the windows send (rof2decomp notes/housing-and-market.md §§2-5; checked against the decompile
// of 0x5b2500, 0x5b7a90, 0x5b0f80, 0x74ab50, 0x74c4c0, 0x5abd20 and ConfirmationDialog_HandleResponse's 0x89).
// None of these are in EQEmu's RoF2 table: a stock EQEmu server never sends them, a server with housing (Project
// Triune) does, with the original's numbers.
//
// Two string forms: records use a u32 length then the bytes (SerializeBuffer_ReadCXStr @0x80c9b0, no NUL); placed
// objects, escrow records and fixed bodies use NUL-terminated strings. The original reads fail-open (a short packet
// leaves defaults and misreads what follows, a missing count can crash it); these decoders refuse a short packet
// instead (a deliberate difference: nothing half-read reaches the game state).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "net/item_packets.h"
#include "net/packet.h"

namespace hailstone::net {

// Server → client, through the real estate dispatcher (0x5b2500).
inline constexpr std::uint16_t kOpReNop1 = 0x0351, kOpReNop2 = 0x3d94; // explicit no-ops
inline constexpr std::uint16_t kOpReItemSummary = 0x087f;    // u32 re, str name, rows
inline constexpr std::uint16_t kOpReItemRows = 0x1cef;       // u32 re, rows
inline constexpr std::uint16_t kOpReItemSummaryUpdate = 0x1097, kOpReItemRowAdd = 0x2cf7; // 0x5afe00
inline constexpr std::uint16_t kOpReItemSummaryRename = 0x1967;
inline constexpr std::uint16_t kOpReItemRemoved = 0x1baf;
inline constexpr std::uint16_t kOpReItemSummaryForget = 0x7d28;
inline constexpr std::uint16_t kOpReResult = 0x1228, kOpReSellResult = 0x66bf, kOpReTransferResult = 0x68ba;
inline constexpr std::uint16_t kOpReSearch = 0x2691;         // both directions
inline constexpr std::uint16_t kOpReCatalog = 0x2698;
inline constexpr std::uint16_t kOpReRecord = 0x63fd, kOpReSetCurrent = 0x6fd0, kOpReRemove = 0x4f09;
inline constexpr std::uint16_t kOpReAccess = 0x3968, kOpReAccessUpdate = 0x485d;
inline constexpr std::uint16_t kOpRePurchase = 0x45be;       // C→S buy; S→C u8 ok (shows the manage window)
inline constexpr std::uint16_t kOpReEscrow = 0x2aa6;         // both directions
inline constexpr std::uint16_t kOpReOwnedA = 0x2aa7, kOpReOwnedB = 0x3e00, kOpReOwnedC = 0x66df;
inline constexpr std::uint16_t kOpReOwnedAToggle = 0x76c6, kOpReOwnedBToggle = 0x0c13;
inline constexpr std::uint16_t kOpReNpcItem = 0x21ea;
inline constexpr std::uint16_t kOpReContextMenu = 0x21ec;    // both directions, 768-byte body
inline constexpr std::uint16_t kOpReInspect = 0x58c7;        // C→S {re, item}; S→C an item
inline constexpr std::uint16_t kOpReOffer = 0x5a63;          // both directions, 0xbc-byte body
// Server → client, own cases of HandleWorldMessage.
inline constexpr std::uint16_t kOpRePlacedItem = 0x1045, kOpRePlacedItemRemove = 0x2a9e;
inline constexpr std::uint16_t kOpReAreas = 0x49df;
inline constexpr std::uint16_t kOpRePlace = 0x626e;          // C→S request; S→C reply
inline constexpr std::uint16_t kOpRePlotSearchOpen = 0x2b35;
// Client → server only.
inline constexpr std::uint16_t kOpReTravel = 0x218c, kOpRePlotSelect = 0x796c, kOpReSell = 0x792c,
                               kOpReTransfer = 0x507f, kOpReOptOut = 0x6573, kOpReAccessSave = 0x3146,
                               kOpRePlaceConfirm = 0x711a, kOpReToStorage = 0x3424, kOpReToInventory = 0x2036,
                               kOpReStoreAll = 0x6917, kOpRePackCrate = 0x613d,
                               kOpReLeave = 0x1da9; // /leaverealestate
// 0x06c8 is not real estate's: its one sender is CContainerMgr opening a container.

// Definition types (the name table at 0xb616a0; DefinitionTypeOf @0x7f4fa0 gives 10 for a missing definition).
enum class ReType : std::uint32_t {
    None = 0, Zone = 1, GuildHall = 2, House = 3, Plot = 4, Neighborhood = 5, Town = 6, Crate = 7, GuildPlot = 8,
    Unknown = 10,
};

// One RealEstate record (0x63fd / 0x6fd0 / 0x4f09; RealEstate::Deserialize @0x7fdf00, RealEstate::vf2 @0x7fd840 its
// writer). Wire: u32 id, f18, parent, definition, guild (-1 none), f28; str address; str owner; u32 f34; u32 child
// count; str name (between the count and the ids); u32 children[count]; u8 n, n × {u32 currency, f64 balance};
// u32 upkeep set, u32 f40, u8 opt out.
struct ReRecord {
    std::uint32_t id = 0, f18 = 0, parent = 0xffffffff, definition = 0;
    std::int32_t guild_id = -1;
    std::uint32_t f28 = 0;
    std::string address, owner;
    std::uint32_t f34 = 0;
    std::string name; // +0x38: the 0x5a63 offer's "%1" (else "this real estate")
    std::vector<std::uint32_t> children;
    struct Balance {
        std::uint32_t currency = 0; // 0 coin (copper), else an alternate currency id (a count)
        double amount = 0;
    };
    std::vector<Balance> escrow;
    std::uint32_t upkeep_set = 0, f40 = 0;
    bool opt_out = false;
};
ReRecord read_re_record(ByteReader& r);
void write_re_record(ByteWriter& w, const ReRecord& v);
std::optional<ReRecord> decode_re_record(const AppPacket& p);
AppPacket encode_re_record(std::uint16_t op, const ReRecord& v); // server direction, for tests

// A definition (0x2698's first part; RealEstate::ReadOneDefinition @0x7f4c90): u32 f00..f10, str name, u32
// f11..f16, u32 n, n × u32. Field meanings in the note's §5.3.
struct ReDefinition {
    std::uint32_t id = 0;
    std::int32_t f01 = -1;
    std::uint32_t price_group = 0, upkeep_group = 0;
    std::int32_t capacity = 0, vault_capacity = 0;
    std::uint32_t f06 = 0, f07 = 0, f08 = 0;
    ReType type = ReType::Unknown; // f09
    std::uint32_t category = 0;    // f10: a dbstr (type 0x1b)
    std::string name;
    std::uint32_t f11 = 0x150;
    std::int32_t pet_capacity = -1; // f12; negative: the capacity
    std::array<std::uint32_t, 4> f13_16{};
    std::vector<std::uint32_t> ids;
};
// A cost block (RealEstate::ReadCostsFromPacket @0x7fc6b0): u32 set id, u32 n, n × {u32 key, u32 m, m × {u32
// currency, u32 sub, u64 amount}}. One entry is one combo choice (its key comes back in 0x45be); its leaves together
// are its price.
struct ReCostLeaf {
    std::uint32_t currency = 0, sub = 0;
    std::uint64_t amount = 0;
};
struct ReCostEntry {
    std::uint32_t key = 0;
    std::vector<ReCostLeaf> leaves;
};
struct ReCostSet {
    std::uint32_t id = 0;
    std::vector<ReCostEntry> entries;
};
// 0x2698: u32 n, n definitions, u32 m, m cost sets (both counts required). A full replacement of the catalog.
struct ReCatalog {
    std::vector<ReDefinition> definitions;
    std::vector<ReCostSet> costs;
};
std::optional<ReCatalog> decode_re_catalog(const AppPacket& p);
AppPacket encode(const ReCatalog& v); // server direction, for tests

// An access table (0x48; ReadRecord48 @0x5b7a90, RealEstateAccess::Serialize @0x749420): 7 × {u32 group, u32 granted
// bits}, u32 n, n × {str name, u32 group}. Bits 0..12 are kReAccess*. Names starting '_' are the owner's clubs
// (`_Guild<rank>`).
inline constexpr int kReAccessGroups = 7;
enum : std::uint32_t {
    kReAccessBuffs = 1u << 0, kReAccessEnter = 1u << 1, kReAccessWithdrawUpkeep = 1u << 2, kReAccessRemoveCrate = 1u << 3,
    kReAccessAddCrate = 1u << 4, kReAccessRemoveHouse = 1u << 5, kReAccessAddHouse = 1u << 6, kReAccessMail = 1u << 7,
    kReAccessRemoveItem = 1u << 8, kReAccessAddItem = 1u << 9, kReAccessMoveItem = 1u << 10,
    kReAccessInteract = 1u << 11, kReAccessManage = 1u << 12,
};
inline constexpr int kReAccessBits = 13;
struct ReAccess {
    struct Group {
        std::uint32_t group = 0, bits = 0;
    };
    // The constructor's slots with 0x5b5830's masks: Everyone, Visitor 3, Acquaintance 0x803, Friend 0xe03,
    // Trustee 0x1f87, Co-owner 0x1fff, the seventh 0.
    std::array<Group, kReAccessGroups> groups{{{0, 3}, {1, 3}, {2, 0x803}, {3, 0xe03}, {4, 0x1f87}, {5, 0x1fff}, {6, 0}}};
    struct Member {
        std::string name;
        std::uint32_t group = 0;
    };
    std::vector<Member> members;
};
// 0x3968: u32 re, the table, u32 (read after it; its use, 0x5b59a0 when no area manager exists, is not known).
// 0x485d: u32 re, the table. 0x3146 (C→S, the save): the table alone? see encode_re_access_save.
struct ReAccessMessage {
    std::uint32_t re = 0;
    ReAccess access;
    std::optional<std::uint32_t> tail; // 0x3968's
};
std::optional<ReAccessMessage> decode_re_access(const AppPacket& p);
AppPacket encode(std::uint16_t op, const ReAccessMessage& v); // server direction, for tests

// One item row (0x3c; 0x5abd20): u32 re, u32 item; u8 placed, u32 f04; 7 × u32 counters; str, str name; then the
// item serialized as in OP_ItemPacket after its type (ItemClient::CreateItemClient).
struct ReItemRow {
    std::uint32_t re = 0, item_id = 0;
    bool placed = false;
    std::uint32_t f04 = 0;
    std::array<std::uint32_t, 7> counters{}; // [1] vault used, [2] pets used, [6] total used
    std::string text, name;
    Item item;
};
ReItemRow read_re_item_row(ByteReader& r);
void write_re_item_row(ByteWriter& w, const ReItemRow& v);
// 0x087f: u32 re, str name, u32 n, rows. 0x1cef: the same without the name. 0x1097: u32 re, then a summary update
// (0x5ab4b0, not decoded: kept raw). 0x2cf7: u32 re, one row.
struct ReItemList {
    std::uint32_t re = 0;
    std::optional<std::string> name;
    std::vector<ReItemRow> rows;
};
std::optional<ReItemList> decode_re_item_list(const AppPacket& p); // 0x087f / 0x1cef / 0x2cf7
AppPacket encode(std::uint16_t op, const ReItemList& v);           // server direction, for tests
// 0x1967: u32 re, then the new name (NUL-terminated at +4).
struct ReRename {
    std::uint32_t re = 0;
    std::string name;
};
std::optional<ReRename> decode_re_rename(const AppPacket& p);
// 0x1baf / 0x2a9e: an item gone. 0x1baf: u32 re, u32 item. 0x2a9e (≥ 0x28 bytes): +0x20 re, +0x24 item.
struct ReItemRef {
    std::uint32_t re = 0, item_id = 0;
};
std::optional<ReItemRef> decode_re_item_removed(const AppPacket& p);
std::optional<ReItemRef> decode_re_placed_item_remove(const AppPacket& p);

// One dword: 0x1228, 0x66bf, 0x68ba, 0x7d28, 0x2b35 (a byte).
std::optional<std::int32_t> decode_re_dword(const AppPacket& p);

// 0x2aa7 / 0x3e00 / 0x66df: u32 n, n × u32 (the owned-estate lists A / B / C, replaced). 0x76c6 / 0x0c13: u32 id,
// the byte at +0x68 (1 adds to list A / B, 0 removes).
std::optional<std::vector<std::uint32_t>> decode_re_id_list(const AppPacket& p);
AppPacket encode_re_id_list(std::uint16_t op, const std::vector<std::uint32_t>& ids); // server direction, for tests
struct ReListToggle {
    std::uint32_t id = 0;
    bool add = false;
};
std::optional<ReListToggle> decode_re_list_toggle(const AppPacket& p);

// 0x2691 (S→C): u32 (skipped), u8 owner type. Not 4 / 8: u32 target, then
//   target 4 / 5 (the plot search window): u16 n, str title, n × {u32 icon, u32 type (4 Player / 8 Guild, other rows
//     read and dropped), u32 re, str owner, str address};
//   target 3: nothing; target < 0: u16 n and nothing;
//   others (the neighborhood window), kind 0 / 1: u16 n, u8 show, [u32 header id when n], n × {u32 id, str name, u32
//     guild (-1 none), u32 player plots, used, guild plots, used}; kind 2: u16 n, n × str player, u16 m, m × u32 guild.
// 4 / 8 (form B, one record): u32, u16 auto open, u32 id, parent, definition, guild, f28, str, str, str address, str
//   owner, 4 × u32 (skipped), u8 open flag. A known id gets only its owner and guild (the caching trap).
struct RePlotRow {
    std::uint32_t icon = 0;
    ReType type = ReType::Plot;
    std::uint32_t re = 0;
    std::string owner, address;
};
struct ReNeighborhoodRow {
    std::uint32_t id = 0;
    std::string name;
    std::int32_t guild_id = -1;
    std::uint32_t player_plots = 0, player_used = 0, guild_plots = 0, guild_used = 0;
};
struct ReSearchReply {
    enum Kind { PlotList, Neighborhoods, Members, Record, Ignored } kind = Ignored;
    std::int32_t target = 0;
    std::string title;                  // PlotList
    std::vector<RePlotRow> plots;       // PlotList
    bool show = false;                  // Neighborhoods
    std::uint32_t header_id = 0;        // Neighborhoods
    std::vector<ReNeighborhoodRow> neighborhoods;
    std::vector<std::string> players;   // Members
    std::vector<std::int32_t> guilds;   // Members
    ReRecord record;                    // Record (id, parent, definition, guild, f28, address, owner)
    bool auto_open = false;             // Record: u16 auto open and the u8 flag both set
};
std::optional<ReSearchReply> decode_re_search_reply(const AppPacket& p);
AppPacket encode(const ReSearchReply& v); // server direction, for tests

// 0x2aa6, both directions: the escrow object (RealEstate::EscrowDeserialize @0x748700, its writer 0x748ce0):
// u32 action, u32 re, u32 f8, cstr address (≤ 63), cstr owner (≤ 31), u8, u32, u32 n, n × {u32 currency (0 coin),
// u32 coin type, u64 amount}. Coin types as CRealEstateManageWnd::SendEscrowFromCursor @0x741ee0 makes them: 3
// platinum, 2 gold, 1 silver, 0 copper. From the server it pays coin to the player (the withdraw path); the balance
// shown comes from the record's escrow.
struct ReEscrow {
    std::uint32_t action = 0, re = 0, f8 = 0;
    std::string address, owner;
    std::uint8_t b = 0;
    std::uint32_t c = 0;
    struct Entry {
        std::uint32_t currency = 0, coin_type = 0;
        std::uint64_t amount = 0;
    };
    std::vector<Entry> entries;
};
std::optional<ReEscrow> decode_re_escrow(const AppPacket& p);
AppPacket encode(const ReEscrow& v);

// The 0xbc-byte transaction body of 0x45be / 0x792c / 0x507f and the 0x5a63 offer: u32 re, u32 a, u32 b,
// char[64] at +0x0c, char[32] at +0x4c, char[64] at +0x6c, u32 +0xac, +0xb0, +0xb4, u8 +0xb8, +0xb9, +0xba.
inline constexpr std::size_t kReTransactionSize = 0xbc;
struct ReTransaction {
    std::uint32_t re = 0, a = 0, b = 0;
    std::string text0c, text4c, text6c;
    std::uint32_t ac = 0, b0 = 0, b4 = 0;
    std::uint8_t b8 = 0, b9 = 0, ba = 0;
};
std::optional<ReTransaction> decode_re_transaction(const AppPacket& p);
AppPacket encode(std::uint16_t op, const ReTransaction& v);

// EQPlacedItem (0x1045, and the object in the 0x626e reply; EQPlacedItem_UnSerialize @0x505400): cstr tag (≤ 63), u8
// NPC item, u32 bc (an NPC item's creature scale), u32 0c, cstr key (16 digits), u32 re, u32 item; f32 × 7 scale
// (+0x7c), heading, pitch, roll (+0x80..+0x88, 0..512), then the position (+0x8c..+0x94) in the order
// CDisplay::CreateActor takes it (EQPlacedItem_CreateActor @0x504b60), the actor's: world x, y, z here (EQ's Y, X,
// Z); u8 collision off, u8 wall only, u8 no free mode, u32 wall allowed; u32 × 7: f32 scale min, f32 scale max, f32
// the size Reset Size gives (+0xa0..+0xa8), +0xac a wall heading offset, f32 the heading / pitch the rotation
// resets give (+0xb0 / +0xb4), +0xb8 the light type.
struct RePlacedItem {
    std::string tag, key;
    bool npc_item = false;
    std::uint32_t fbc = 0;
    std::int32_t f0c = -1;
    std::uint32_t re = 0, item_id = 0;
    float scale = 1, heading = 0, pitch = 0, roll = 0;
    glm::vec3 server_pos{0.0f}; // +0x8c..+0x94 as sent: the actor's position, world x, y, z (game::placed_world_position)
    bool no_collision = false, wall_only = false, no_free_mode = false;
    std::uint32_t wall_allowed = 0;
    std::array<std::uint32_t, 7> tail{};
};
RePlacedItem read_re_placed_item(ByteReader& r);
void write_re_placed_item(ByteWriter& w, const RePlacedItem& v);
std::optional<RePlacedItem> decode_re_placed_item(const AppPacket& p);
AppPacket encode(const RePlacedItem& v); // server direction, for tests

// 0x49df: u32 n, n × 88 bytes (EQAreaBase::Deserialize @0x8d2e30): u32 id, f32[16] the local → world matrix (row
// major, the translation in row 3), u32 × 5. The volume is the unit cube [-1, 1]³ in local space.
struct ReArea {
    std::uint32_t id = 0;
    std::array<float, 16> m{};
    std::array<std::int32_t, 5> tail{};
};
std::optional<std::vector<ReArea>> decode_re_areas(const AppPacket& p);
AppPacket encode(const std::vector<ReArea>& v); // server direction, for tests

// 0x21ea: u32 spawn id, cstr guid at +4 (16 characters), +0x18 → spawn +0x198, +0x1c → spawn +0x420.
struct ReNpcItem {
    std::uint32_t spawn_id = 0;
    std::string guid;
    std::uint32_t f18 = 0, f1c = 0;
};
std::optional<ReNpcItem> decode_re_npc_item(const AppPacket& p);

// 0x21ec (768 bytes both ways). The request (0x542a90, from the items window's right click on a row): char[18]
// key (the item's 16-digit serial) at +0, a slot {-1, -1, 0xffff} at +0x14, +0x20 re, +0x24 item, +0x28 0, the name
// zeroed, 40 entries 0xff-filled, u8 1 at +0x2ec, the mouse's x, y at +0x2f0 / +0x2f4, the item's +0xec at +0x2f8.
// The reply (CRealEstateItemsWnd_ShowItemContextMenu @0x7375f0): the same, +0x2c cstr name, 40 entries at +0x6c +
// 0x10 i {i32 type (-1 unused), i32 parameter, u32 text (an eqstr; types 9 and 0xa a dbstr of kind 0x1c), i32 the
// sub-menu's parameter}; with +0x2ec set types 3, 4, 0xb, 0xe and type 2 with parameter 1 are left out. A chosen
// entry's type picks what the window does (CRealEstateItemsWnd::WndNotification's 0x65 + index arms).
// A right click in the world (CEverQuest::RMouseUp on a placed object or a real estate NPC item) calls the same
// builder with the empty key (sixteen '0'), +0x2ec 0 and +0x2f8 -1.
inline constexpr std::size_t kReMenuSize = 768;
struct ReMenu {
    std::string key, name;
    std::uint32_t re = 0, item = 0;
    struct Entry {
        int index = 0; // 0..39: the menu id is 0x65 + index
        std::int32_t type = -1, param = 0;
        std::uint32_t text = 0;
        std::int32_t sub = -1;
    };
    std::vector<Entry> entries; // the used ones (type != -1), in order
    bool restricted = false;    // +0x2ec
    std::int32_t x = 0, y = 0;
    std::uint32_t f2f8 = 0, f2fc = 0;
};
std::optional<ReMenu> decode_re_menu(const AppPacket& p);
AppPacket encode(const ReMenu& v); // the reply (server direction, for tests)
AppPacket encode_re_menu_request(std::string_view key, std::uint32_t re, std::uint32_t item, std::int32_t x,
                                 std::int32_t y, std::uint32_t f2f8);
AppPacket encode_re_menu_request(std::uint32_t re, std::uint32_t item, std::int32_t x, std::int32_t y);
// The menu's other requests (WndNotification's arms): types 8 / 9 / 0x13 send {re, item, the entry's parameter} on
// 0x050c / 0x51e3 / 0x64a3; type 5 sends 0x7ad1 {re, item, 140 zero bytes}. Their meaning is not known.
inline constexpr std::uint16_t kOpReMenu8 = 0x050c, kOpReMenu9 = 0x51e3, kOpReMenu13 = 0x64a3, kOpReMenu5 = 0x7ad1;
AppPacket encode_re_item_op(std::uint16_t op, std::uint32_t re, std::uint32_t item, std::uint32_t param);
AppPacket encode_re_menu5(std::uint32_t re, std::uint32_t item);

// 0x58c7 (S→C): an item to inspect, serialized as in OP_ItemPacket after its type.
std::optional<Item> decode_re_inspect(const AppPacket& p);

// Client → server.
// 0x2691: subtype (0 my plots / search, 1 my items, 2 a neighborhood row, 4 the plot search window), id, 0, char[80]
// at +0x0c, char[64] at +0x5c (the plot search's name: 0x74b920), char[64] at +0x9c, u8 flag +0xdc, u8 1
// +0xdd, 2 bytes of padding (224 bytes).
AppPacket encode_re_search(std::uint32_t subtype, std::uint32_t id, std::string_view s0 = {}, std::string_view s1 = {},
                           std::string_view s2 = {}, std::uint8_t flag = 0);
// 0x218c: {u32 id, u32 secondary (-1 none), u32 0}: travel to a neighborhood, or into a plot's house.
AppPacket encode_re_travel(std::uint32_t id, std::uint32_t secondary);
AppPacket encode_re_plot_select(std::uint32_t plot); // 0x796c
// 0x45be (buy): re at +0, the price key at +0xac, the upkeep key at +0xb0, +0xb4 the byte at 0xdd5d00 (0 here).
AppPacket encode_re_purchase(std::uint32_t re, std::uint32_t price_key, std::uint32_t upkeep_key);
AppPacket encode_re_sell(std::uint32_t re);                                 // 0x792c {re, -1, -1}
AppPacket encode_re_transfer(std::uint32_t re, std::string_view target);    // 0x507f, the name at +0x6c
AppPacket encode_re_opt_out(std::uint32_t re, bool opt_out);                // 0x6573
// 0x2aa6 (C→S): SendEscrowFromCursor's actions: 0 the cursor's coin in (one entry {0, coin type, amount}), 1 one
// denomination out (the same entry), 3 Add Day, 4 Add Week, 7 Withdraw All (no entries, as captured), 2 the upkeep
// plan (not built here).
enum class ReEscrowAction : std::uint32_t { Deposit = 0, Withdraw = 1, SetUpkeep = 2, AddDay = 3, AddWeek = 4, WithdrawAll = 7 };
AppPacket encode_re_escrow(ReEscrowAction action, std::uint32_t re, std::vector<ReEscrow::Entry> entries = {});
// 0x3146: one estate's access table (RealEstateAccess::Serialize): u32 re then the table.
AppPacket encode_re_access_save(std::uint32_t re, const ReAccess& access);
// 0x3424 / 0x2036 (move to storage / inventory; 0x73f6c0 / 0x542470): char[18] key (the item's serial) at
// +0, a slot {-1, -1, 0xffff} at +0x14, +0x20 re, +0x24 item, +0x28 0, and for storage the destination at +0x2c: the
// container index of the estate's type (0x7f5ec0: 2 a house (closet), 3 a plot (shed), 6 a crate). 48 / 44 bytes.
AppPacket encode_re_to_storage(std::string_view key, std::uint32_t re, std::uint32_t item, std::uint32_t destination);
AppPacket encode_re_to_inventory(std::string_view key, std::uint32_t re, std::uint32_t item);
// 0x7f5ec0: an estate type's container index (the storage destinations).
std::uint32_t re_container_index(ReType type);
AppPacket encode_re_store_all();          // 0x6917
AppPacket encode_re_pack_crate(bool pack); // 0x613d
AppPacket encode_re_inspect(std::uint32_t re, std::uint32_t item); // 0x58c7
// ItemGlobalIndex as the placement packets carry it: on the wire (0x4d60e0) u8 location then u16 slot, sub, aug
// (7 bytes); in the fixed bodies {i32 location, i16 slot, i16 sub, i16 aug, 2 bytes} (12 bytes).
struct ReItemIndex {
    std::int32_t location = -1;
    std::int16_t slot = -1, sub = -1, aug = -1;
};
inline constexpr std::int16_t kReCursorSlot = 33;
// 0x626e (C→S, 60 bytes; 0x5424d0's defaults, then CEverQuest::LMouseUp with a placeable item on the cursor or
// the items window's Place): +0x00 the item's serial (16 characters + u16), +0x14 the item index (the cursor: {0,
// 33, -1, -1}), +0x20 re, +0x24 item (an item already placed: its row's ids, the index left at -1s; else -1, -1),
// +0x28 0, +0x2c 1, +0x30 u8 1, +0x34 / +0x38 the mouse's screen x, y.
struct RePlaceRequest {
    std::string key = "0000000000000000";
    ReItemIndex index;
    std::uint32_t re = 0xffffffffu, item = 0xffffffffu;
    std::int32_t x = 0, y = 0;
};
AppPacket encode(const RePlaceRequest& v);
std::optional<RePlaceRequest> decode_re_place_request(const AppPacket& p); // server side, for tests
// The cursor's item at slot 33 (the old form; the items window's own row uses RePlaceRequest).
AppPacket encode_re_place_request(std::string_view key, std::uint16_t slot, std::int32_t x, std::int32_t y);
// 0x626e (S→C; HandleWorldMessage @0x4d058a): the object to place (EQPlacedItem_UnSerialize), the item index (7
// bytes), u8 b1, b2, b3, u32 c1, c2, c3, then (only while the area manager exists: always here) u32 n, n × {u32
// key, u32 code} and one more {key, code}: AreaManager map 2, the codes AreaManager::Query returns (0 allowed). Then
// BeginPlacement @0x558bb0 (listener, object, index, b1, b2, b3, c1, c2, c3, 0).
struct RePlaceReply {
    RePlacedItem item;
    ReItemIndex index;
    std::uint8_t b1 = 0, b2 = 0, b3 = 0;
    std::uint32_t c1 = 0, c2 = 0, c3 = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> codes; // all n + 1 entries, in order
};
std::optional<RePlaceReply> decode_re_place_reply(const AppPacket& p);
AppPacket encode(const RePlaceReply& v); // server direction, for tests
// 0x711a (96 bytes; Send_Opcode_0x711a @0x5b1f90 → RealEstate::SerializeConfirm @0x5b3fb0): +0x00 the key (18
// bytes), +0x14 the item index (12 bytes), +0x20 re, +0x24 item, +0x28 0, +0x2c heading, pitch, roll, scale, x,
// y, z (f32), +0x48 / +0x54 the actor's bounding box (0x4907f0: the second vec3, then the first).
AppPacket encode_re_place_confirm(const RePlacedItem& v, const ReItemIndex& index = {}, const glm::vec3& box_a = {},
                                  const glm::vec3& box_b = {});
struct RePlaceConfirm {
    std::string key;
    ReItemIndex index;
    std::uint32_t re = 0, item = 0;
    float heading = 0, pitch = 0, roll = 0, scale = 1;
    glm::vec3 pos{0.0f}, box_a{0.0f}, box_b{0.0f};
};
std::optional<RePlaceConfirm> decode_re_place_confirm(const AppPacket& p); // server side, for tests
// 0x6213 (S→C, any body; HandleWorldMessage): ItemPlacement_Stop, the placement cancelled.
inline constexpr std::uint16_t kOpRePlaceStop = 0x6213;

} // namespace hailstone::net
