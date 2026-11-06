// Using items (net/game_connection_item_use.cpp): CInvSlot::HandleRButtonUp's request, Handle_OP_ItemVerifyReply's
// eating and casting and Cmd_useitem's arguments, against the mock EQEmu. (The NMS plugin's scribe path is tested in
// its own repository.)

#include <doctest/doctest.h>

#include <cstring>
#include <ctime>

#include "mock_server/harness.h"
#include "net/ability_packets.h"
#include "net/item_packets.h"

using namespace nmsc;
using namespace nmsc::net;
using namespace nmsc::test;

namespace {

std::uint32_t u32_at(const AppPacket& p, std::size_t at) {
    std::uint32_t v = 0;
    std::memcpy(&v, p.data.data() + at, 4);
    return v;
}

const AppPacket* last_of(const std::vector<AppPacket>& log, Op op) {
    for (auto it = log.rbegin(); it != log.rend(); ++it)
        if (from_wire(it->opcode) == op) return &*it;
    return nullptr;
}

std::vector<std::string> drain_chat(GameConnection& c) {
    std::vector<std::string> lines;
    while (auto e = c.poll_chat()) lines.push_back(e->text);
    return lines;
}

bool has_line(const std::vector<std::string>& lines, const std::string& text) {
    for (const auto& l : lines)
        if (l == text) return true;
    return false;
}

MockConfig with_items(std::vector<Item> extra) {
    MockConfig c = MockConfig::standard();
    auto& inv = c.characters[0].inventory;
    inv[1].item_type = kItemTypeFood; // the Rations in general slot 1 (23), five of them
    inv[1].cast_time_ = 30;          // FoodDuration
    for (auto& i : extra) inv.push_back(std::move(i));
    return c;
}

} // namespace

TEST_CASE("item use: a right click sends OP_ItemVerifyRequest; the reply eats the food") {
    Rig rig(with_items({}));
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    const ItemSlot rations = ItemSlot::possessions(23);
    CHECK(conn.right_click_item(rations) == GameConnection::SlotRightClick::None);
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, Op::ItemVerifyRequest) == 1; }));
    const AppPacket* req = last_of(rig.server.zone_log, Op::ItemVerifyRequest);
    REQUIRE(req);
    REQUIRE(req->data.size() == kItemVerifyRequestSize);
    CHECK(u32_at(*req, 0) == 0);           // possessions
    CHECK(u32_at(*req, 4) == (23u | 0xffffu << 16)); // slot 23, no bag index
    CHECK(u32_at(*req, 12) == 0);          // no target
    CHECK(conn.spell_state().pending == 1);
    // IsItemPending: a second click waits for the answer.
    conn.right_click_item(rations);
    rig.run_for(std::chrono::milliseconds(30));
    CHECK(MockEqServer::count(rig.server.zone_log, Op::ItemVerifyRequest) == 1);

    // EQEmu's reply (spell 0): hunger 5999 + 30 x 100 / 2 caps at 6000, OP_Consume {slot, -1, 1, 1}, one fewer.
    drain_chat(conn);
    conn.receive_zone_packet_for_test(encode(ItemVerifyReply{rations, 0, 0}));
    CHECK(conn.spell_state().pending == 0);
    CHECK(conn.spell_state().hunger == 6000);
    CHECK(conn.inventory().at(rations)->stack_count == 4);
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, Op::Consume) == 1; }));
    const AppPacket* eat = last_of(rig.server.zone_log, Op::Consume);
    REQUIRE(eat->data.size() == 24);
    CHECK(u32_at(*eat, 12) == 0xffffffffu);
    CHECK(u32_at(*eat, 16) == 1);
    CHECK(u32_at(*eat, 20) == 1);

    // Full now: 12074, nothing eaten or sent.
    conn.receive_zone_packet_for_test(encode(ItemVerifyReply{rations, 0, 0}));
    CHECK(conn.inventory().at(rations)->stack_count == 4);
    rig.run_for(std::chrono::milliseconds(30));
    CHECK(MockEqServer::count(rig.server.zone_log, Op::Consume) == 1);
    // A negative spell stops at the dispatch, after taking back the pending count (none left: stays 0).
    conn.receive_zone_packet_for_test(encode(ItemVerifyReply{rations, -1, 0}));
    CHECK(conn.spell_state().pending == 0);
}

TEST_CASE("item use: a food duration past 2^31 / 100 wraps as the original's 32-bit FoodDuration x 100 / 2") {
    // 0x02000000 x 100 is 0xc8000000 in 32 bits, -939524096, so half of it is -469762048: the hunger goes down from
    // 5999 to -469756049 (stored as the u32 the meter keeps), as the original's imul / idiv gives, on every compiler.
    MockConfig config = with_items({});
    config.characters[0].inventory[1].cast_time_ = 0x02000000;
    Rig rig(config);
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    const ItemSlot rations = ItemSlot::possessions(23);
    conn.receive_zone_packet_for_test(encode(ItemVerifyReply{rations, 0, 0}));
    CHECK(conn.spell_state().hunger == static_cast<std::uint32_t>(5999 - 469762048));
}

TEST_CASE("item use: a bag toggles its container, the bank and corpse paths send no request") {
    Item bag = make_item(1003, "Backpack", 24);
    bag.item_class = 1;
    bag.bag_slots = 8;
    Rig rig(with_items({bag}));
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    CHECK(conn.right_click_item(ItemSlot::possessions(24)) == GameConnection::SlotRightClick::ToggleContainer);
    // Alt: the item preview (not built), no request.
    CHECK(conn.right_click_item(ItemSlot::possessions(13), {false, false, true}) ==
          GameConnection::SlotRightClick::ItemPreview);
    // An empty slot: nothing.
    CHECK(conn.right_click_item(ItemSlot::possessions(30)) == GameConnection::SlotRightClick::None);
    rig.run_for(std::chrono::milliseconds(30));
    CHECK(MockEqServer::count(rig.server.zone_log, Op::ItemVerifyRequest) == 0);
    // The sword (a common item, no click spell) still goes to the server.
    conn.right_click_item(ItemSlot::possessions(13));
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, Op::ItemVerifyRequest) == 1; }));
}

TEST_CASE("item use: Cmd_useitem's arguments") {
    Item bag = make_item(1003, "Backpack", 24);
    bag.item_class = 1;
    bag.bag_slots = 4;
    BagItem in;
    in.index = 2;
    in.item = make_item(1004, "Gem", 24);
    bag.contents.push_back(in);
    Rig rig(with_items({bag}));
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    drain_chat(conn);
    conn.send_command("/useitem");
    auto lines = drain_chat(conn);
    CHECK(has_line(lines, "usage: /useitem <slot> <subindex>"));
    CHECK(has_line(lines, "       Power Source = 21"));
    CHECK(has_line(lines, "       Bag Slot 10 = 32"));
    conn.send_command("/useitem 40");
    CHECK(has_line(drain_chat(conn), "That is an invalid slot index."));
    // A bad bag index keeps the bag itself; a good one the item in it.
    conn.send_command("/useitem 24 9");
    CHECK(has_line(drain_chat(conn), "That is an invalid bag slot index."));
    conn.send_command("/useitem 24 2"); // (the bag itself is no common item: nothing went for "24 9")
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, Op::ItemVerifyRequest) == 1; }));
    const AppPacket* req = last_of(rig.server.zone_log, Op::ItemVerifyRequest);
    CHECK(u32_at(*req, 4) == (24u | 2u << 16));
}

TEST_CASE("item use: a poison applies through OP_ApplyPoison (CEverQuest::ApplyPoison)") {
    Item poison = make_item(14001, "Spider Venom", 25, 5);
    poison.item_type = kItemTypePoison;
    Rig rig(with_items({poison}));
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    const ItemSlot at = ItemSlot::possessions(25);
    conn.receive_zone_packet_for_test(encode_skill_update(SkillUpdate{6, 0, 0})); // (the mock's skill n is n)
    drain_chat(conn);
    // No Apply Poison skill: 12383, still standing.
    conn.receive_zone_packet_for_test(encode(ItemVerifyReply{at, 0, 0}));
    CHECK(has_line(drain_chat(conn), "[12383]"));
    CHECK_FALSE(conn.applying_poison());
    CHECK_FALSE(conn.sitting());

    conn.receive_zone_packet_for_test(encode_skill_update(SkillUpdate{6, 50, 0}));
    drain_chat(conn);
    // Standing: sits down, then begins.
    conn.receive_zone_packet_for_test(encode(ItemVerifyReply{at, 0, 0}));
    CHECK(has_line(drain_chat(conn), "[12382]"));
    CHECK(conn.sitting());
    CHECK(conn.applying_poison());
    // Standing up cancels it.
    conn.set_stand_state(100);
    CHECK(has_line(drain_chat(conn), "[12497]"));
    CHECK_FALSE(conn.applying_poison());

    // Again, and let the time run out: OP_ApplyPoison {25, -1, -1, 0, 0}, both counters raised.
    conn.receive_zone_packet_for_test(encode(ItemVerifyReply{at, 0, 0}));
    REQUIRE(conn.applying_poison());
    conn.update();
    CHECK(MockEqServer::count(rig.server.zone_log, Op::ApplyPoison) == 0);
    conn.expire_poison_timer_for_test();
    conn.update();
    CHECK_FALSE(conn.applying_poison());
    CHECK(conn.spell_state().pending == 1);
    CHECK(conn.transact_state().currently_interacting == 1);
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, Op::ApplyPoison) == 1; }));
    const AppPacket* sent = last_of(rig.server.zone_log, Op::ApplyPoison);
    REQUIRE(sent->data.size() == kApplyPoisonSize);
    CHECK(u32_at(*sent, 0) == (25u | 0xffffu << 16));
    CHECK(u32_at(*sent, 4) == 0xffffu);
    CHECK(u32_at(*sent, 8) == 0);
    // While it is out, another poison does not start.
    conn.receive_zone_packet_for_test(encode(ItemVerifyReply{at, 0, 0}));
    CHECK_FALSE(conn.applying_poison());

    // The reply ends the interaction and reports.
    drain_chat(conn);
    conn.receive_zone_packet_for_test(encode(ApplyPoison{25, -1, -1, 0, 1}));
    CHECK(has_line(drain_chat(conn), "[12905]"));
    CHECK(conn.spell_state().pending == 0);
    CHECK(conn.transact_state().currently_interacting == 0);
    conn.receive_zone_packet_for_test(encode(ApplyPoison{25, -1, -1, 0, 0}));
    CHECK(has_line(drain_chat(conn), "[12906]"));
}

TEST_CASE("item recast timers: the item's own and the type's (GetItemRecastTimer), OP_ItemRecastDelay") {
    // Two clickies: a recast type 5 one and one without a type (-1), whose header carries its own timer.
    const auto now = static_cast<std::uint32_t>(std::time(nullptr));
    Item typed = make_item(1100, "Typed Clicky", 27);
    typed.effects[kEffectClick].effect = 200;
    typed.effects[kEffectClick].extra[2] = 30; // recast delay (s)
    typed.effects[kEffectClick].extra[3] = 5;  // recast type
    Item own = make_item(1101, "Own Clicky", 28);
    own.effects[kEffectClick].effect = 201;
    own.effects[kEffectClick].extra[2] = 60;
    own.effects[kEffectClick].extra[3] = 0xffffffffu;
    own.last_cast_time = now + 40; // ItemSerializationHeader last_cast_time
    Rig rig(with_items({typed, own}));
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    const Item* t = conn.inventory().at(ItemSlot::possessions(27));
    const Item* o = conn.inventory().at(ItemSlot::possessions(28));
    REQUIRE(t);
    REQUIRE(o);
    CHECK(conn.item_recast_timer(t) == 0);
    CHECK(conn.item_recast_timer(o) == now + 40);
    CHECK(conn.item_recast_timer(nullptr) == 0);

    // No item cast under way: the packet is taken only with ignore_casting_requirement, then only the type's.
    AppPacket d(to_wire(Op::ItemRecastDelay), std::vector<std::byte>(12));
    const std::uint32_t delay = 30, type = 5;
    std::memcpy(d.data.data(), &delay, 4);
    std::memcpy(d.data.data() + 4, &type, 4);
    conn.receive_zone_packet_for_test(d);
    CHECK(conn.item_recast_timer(t) == 0);
    d.data[8] = std::byte{1};
    conn.receive_zone_packet_for_test(d);
    const std::uint32_t got = conn.item_recast_timer(t);
    CHECK(got >= now + 30);
    CHECK(got <= now + 32);
    CHECK(conn.item_recast_type_timer(5) == got);
    CHECK(conn.item_recast_timer(o) == now + 40); // type -1 keeps only its own
}

TEST_CASE("item clicks: CastSpell's attune and CanUseItem checks refuse quietly (@0x43b088, @0x43b0b6)") {
    // A self-only instant spell, castable in and out of combat (Sympathetic Strike II's shape).
    std::vector<std::string> f(237, "0");
    f[0] = "500";
    f[1] = "Test Click";
    f[9] = "100";
    f[86] = "254";
    f[98] = "6";   // self
    f[213] = "1";  // in combat
    f[214] = "1";  // out of combat
    std::string line;
    for (std::size_t i = 0; i < f.size(); ++i) line += (i ? "^" : "") + f[i];
    auto table = std::make_shared<game::SpellTable>();
    REQUIRE(table->parse(line) == 1);

    auto clicky = [](std::uint32_t id, const char* name, std::int16_t slot, std::uint32_t type) {
        Item it = make_item(id, name, slot);
        it.effects[kEffectClick].effect = 500;
        it.effects[kEffectClick].type = type;
        it.charges = 1;
        return it;
    };
    // The Ancient Sensate Eye (Legendary) case: attuneable, click type 5, not attuned.
    Item eye = clicky(1200, "Attuneable Eye", 27, 5);
    eye.attuneable = 1;
    eye.attuned = 0;
    // Click type 5 with a class the character (a warrior) is not.
    Item wand = clicky(1201, "Cleric Wand", 28, 5);
    wand.classes = 1u << 1;
    // Click type 4 (must equip) from a general slot.
    Item pick = clicky(1202, "Must Equip Pick", 29, 4);
    // The eye attuned (NoDropFlag +0x80, the header's inst_nodrop).
    Item attuned = eye;
    attuned.slot = ItemSlot::possessions(30);
    attuned.attuned = 1;
    Rig rig(with_items({eye, wand, pick, attuned}));
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    conn.set_spell_table_for_test(table);
    auto casts = [&] { return MockEqServer::count(rig.server.zone_log, Op::CastSpell); };
    auto reply = [&](std::int16_t slot) {
        drain_chat(conn);
        conn.receive_zone_packet_for_test(encode(ItemVerifyReply{ItemSlot::possessions(slot), 0, 0}));
        rig.run_for(std::chrono::milliseconds(30));
        return drain_chat(conn);
    };

    // Each refusal is silent for an item cast ([esp+0x1c] is 0 with an item): no 7586, 179 or CanUseItem line.
    CHECK(reply(27).empty());
    CHECK(reply(28).empty());
    CHECK(reply(29).empty());
    CHECK(casts() == 0);

    // Attuned, the eye casts from a general slot.
    reply(30);
    REQUIRE(rig.until([&] { return casts() == 1; }));
    const AppPacket* sent = last_of(rig.server.zone_log, Op::CastSpell);
    CHECK(u32_at(*sent, 0) == 12);  // the item gem
    CHECK(u32_at(*sent, 4) == 500);
    CHECK(u32_at(*sent, 12) == (30u | 0xffffu << 16));
}

TEST_CASE("auto inventory: worn stacks first, then a worn slot by the class check alone (AutoInventoryCursorItem)") {
    Item worn;
    worn.id = 8005;
    worn.name = "Field Point Arrow";
    worn.item_type = 27;
    worn.max_stack = 100;
    worn.stack_count = 90;
    worn.classes = 0xffff;
    worn.equip_slots = 1u << kSlotAmmo;
    worn.slot = ItemSlot::possessions(kSlotAmmo);
    Item held = worn;
    held.stack_count = 15;
    held.slot = ItemSlot::possessions(kSlotCursor);
    Rig rig(with_items({worn, held}));
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    REQUIRE(conn.inventory().cursor());
    CHECK(conn.auto_inventory());
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, Op::MoveItem) >= 2; }));
    std::vector<MoveItem> moves;
    for (const auto& p : rig.server.zone_log)
        if (from_wire(p.opcode) == Op::MoveItem)
            if (auto mv = decode_move_item(p)) moves.push_back(*mv);
    REQUIRE(moves.size() == 2);
    CHECK(moves[0].to.slot == kSlotAmmo);
    CHECK(moves[0].count == 10); // the 10 that fit on the 90
    CHECK(moves[1].to.slot >= kSlotGeneral1); // the other 5 to a free general slot
    CHECK(moves[1].to.slot <= kSlotGeneral10);
    CHECK_FALSE(conn.inventory().cursor());
}

TEST_CASE("the inventory doll: a held item is auto-inventoried (CInventoryWnd::HandleLButtonUp)") {
    Item held;
    held.id = 1001;
    held.name = "Cloth Cap";
    held.slot = ItemSlot::possessions(kSlotCursor);
    Rig rig(with_items({held}));
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    REQUIRE(conn.inventory().cursor());
    conn.drop_on_doll();
    CHECK_FALSE(conn.inventory().cursor());
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, Op::MoveItem) >= 1; }));
}
