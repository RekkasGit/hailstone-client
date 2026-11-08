// Looting a corpse (net/game_connection_items.cpp): CEverQuest::LootCorpse's checks, Handle_OP_MoneyOnCorpse's
// answers and coin text (FormatCoinAmountsText), Handle_OP_LootItem's refusal and Loot All's close, and
// unk_CLootWnd_ResetLootSession, against the mock EQEmu (whose string table prints "[id] args").

#include <doctest/doctest.h>

#include "game/inventory.h"
#include "game/item_options.h"
#include "mock_server/harness.h"
#include "net/item_packets.h"
#include "net/packet_io.h"
#include "net/trade_packets.h"

using namespace hailstone;
using namespace hailstone::net;
using namespace hailstone::test;

namespace {

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

// A corpse `offset` units east of the player.
std::uint32_t add_corpse(GameConnection& conn, float offset) {
    auto& w = conn.world_for_test();
    const game::Spawn* me = w.player();
    REQUIRE(me);
    game::Spawn s;
    s.id = 4000;
    s.name = "a_rat's_corpse01";
    s.is_npc = true;
    s.is_corpse = true;
    s.position = me->position + glm::vec3(offset, 0.0f, 0.0f);
    s.heights.avatar_height = 2.0f;
    w.spawns[s.id] = s;
    return s.id;
}

AppPacket money(std::uint8_t response, std::uint32_t p = 0, std::uint32_t g = 0, std::uint32_t s = 0,
                std::uint32_t c = 0) {
    MoneyOnCorpse m;
    m.response = response;
    m.platinum = p;
    m.gold = g;
    m.silver = s;
    m.copper = c;
    return encode(m);
}

AppPacket loot_item_packet(std::int16_t slot, const std::string& name) {
    ItemPacket ip;
    ip.type = kItemPacketLoot;
    ip.item.id = 1001;
    ip.item.name = name;
    ip.item.slot.type = 0;
    ip.item.slot.slot = slot;
    ip.item.stack_count = 1;
    return encode_item_packet(false, ip);
}

} // namespace

TEST_CASE("loot: FormatCoinAmountsText's English coin text") {
    CHECK(game::coin_amounts_text(1, 2, 3, 4) == "1 platinum, 2 gold, 3 silver and 4 copper");
    CHECK(game::coin_amounts_text(1, 0, 0, 4) == "1 platinum and 4 copper");
    CHECK(game::coin_amounts_text(1, 2, 0, 0) == "1 platinum and 2 gold");
    CHECK(game::coin_amounts_text(1, 2, 0, 4) == "1 platinum, 2 gold and 4 copper");
    CHECK(game::coin_amounts_text(0, 0, 3, 0) == "3 silver");
    CHECK(game::coin_amounts_text(0, 0, 0, 0).empty());
}

TEST_CASE("loot: /loot without a target, a corpse too far, and the request") {
    Rig rig(MockConfig::standard());
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    drain_chat(conn);
    conn.set_target(0);
    conn.send_command("/loot");
    CHECK(has_line(drain_chat(conn), "[13201]"));

    // Beyond AvatarHeight * 1.6 + 14 = 17.2: refused here.
    std::uint32_t corpse = add_corpse(conn, 30.0f);
    conn.loot(corpse);
    CHECK(has_line(drain_chat(conn), "[12390]"));
    CHECK_FALSE(conn.loot_session());

    conn.world_for_test().spawns[corpse].position = conn.world_for_test().player()->position + glm::vec3(10, 0, 0);
    conn.loot(corpse);
    REQUIRE(conn.loot_session());
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, Op::LootRequest) == 1; }));
    // A second request while one corpse is open does nothing.
    conn.loot(corpse);
    rig.run_for(std::chrono::milliseconds(30));
    CHECK(MockEqServer::count(rig.server.zone_log, Op::LootRequest) == 1);
}

TEST_CASE("loot: Handle_OP_MoneyOnCorpse's answers") {
    Rig rig(MockConfig::standard());
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    std::uint32_t corpse = add_corpse(conn, 5.0f);

    SUBCASE("refusals say why and let the corpse go") {
        const std::pair<std::uint8_t, const char*> cases[] = {
            {0, "[12073]"},
            {2, "[12070]"},
            {4, "[5154]"},
            {5, "[12390]"},
        };
        for (const auto& [response, text] : cases) {
            conn.loot(corpse);
            REQUIRE(conn.loot_session());
            drain_chat(conn);
            conn.receive_zone_packet_for_test(money(response));
            CHECK(has_line(drain_chat(conn), text));
            CHECK_FALSE(conn.loot_session());
        }
        conn.loot(corpse);
        conn.receive_zone_packet_for_test(money(7)); // closes without a word
        CHECK(drain_chat(conn).empty());
        CHECK_FALSE(conn.loot_session());
    }

    SUBCASE("the coin, the looting stand state, then OP_LootComplete") {
        conn.set_target(corpse);
        conn.loot(corpse);
        auto before = conn.coins();
        drain_chat(conn);
        conn.receive_zone_packet_for_test(money(1, 1, 2, 3, 4));
        CHECK(has_line(drain_chat(conn), "[12072] 1 platinum, 2 gold, 3 silver and 4 copper"));
        CHECK(conn.coins().platinum == before.platinum + 1);
        CHECK(conn.coins().copper == before.copper + 4);
        REQUIRE(conn.loot_session());
        CHECK(conn.loot_session()->open);
        CHECK(conn.player_status().stand_state == kStandLooting);

        conn.receive_zone_packet_for_test(loot_item_packet(23, "Cloth Cap"));
        conn.receive_zone_packet_for_test(loot_item_packet(24, "Rusty Dagger"));
        CHECK(conn.loot_session()->items.size() == 2);
        // A refused item stays ("[1370]").
        LootItem refused;
        refused.lootee = corpse;
        refused.slot = 23;
        refused.auto_loot = -1;
        conn.receive_zone_packet_for_test(encode(refused));
        CHECK(has_line(drain_chat(conn), "[1370]"));
        CHECK(conn.loot_session()->items.size() == 2);
        LootItem taken = refused;
        taken.auto_loot = 1;
        conn.receive_zone_packet_for_test(encode(taken));
        CHECK(conn.loot_session()->items.size() == 1);

        conn.receive_zone_packet_for_test(AppPacket{to_wire(Op::LootComplete), {}});
        CHECK_FALSE(conn.loot_session());
        CHECK(conn.world().target_id == 0);
        CHECK(conn.player_status().stand_state == kStandStanding);
    }

    SUBCASE("with /autosplit on the coin is 'your split'") {
        conn.send_command("/autosplit on");
        conn.loot(corpse);
        drain_chat(conn);
        conn.receive_zone_packet_for_test(money(1, 0, 0, 5, 0));
        CHECK(has_line(drain_chat(conn), "[12071] 5 silver"));
    }
}

TEST_CASE("loot: Loot All waits for every item, then closes the window after the last answer") {
    Rig rig(MockConfig::standard());
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    std::uint32_t corpse = add_corpse(conn, 5.0f);
    conn.loot(corpse);
    conn.receive_zone_packet_for_test(money(1));
    conn.receive_zone_packet_for_test(loot_item_packet(23, "Cloth Cap"));
    conn.receive_zone_packet_for_test(loot_item_packet(24, "Rusty Dagger"));
    // Before the server's "every item sent" (6) Loot All only remembers it was asked for.
    conn.set_client_option_for_session(game::ClientOption::LootAllConfirm, 0);
    conn.set_client_option_for_session(game::ClientOption::LootNoDrop, 2); // never asks about no-drop items
    conn.destroy_cursor(); // the mock character holds coin, which stops Loot All
    conn.loot_all();
    rig.run_for(std::chrono::milliseconds(30));
    CHECK(MockEqServer::count(rig.server.zone_log, Op::LootItem) == 0);
    conn.receive_zone_packet_for_test(money(6));
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, Op::LootItem) == 1; }));
    LootItem answer;
    answer.lootee = corpse;
    answer.slot = 23;
    answer.auto_loot = 1;
    conn.receive_zone_packet_for_test(encode(answer));
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, Op::LootItem) == 2; }));
    CHECK(MockEqServer::count(rig.server.zone_log, Op::EndLootRequest) == 0);
    answer.slot = 24;
    conn.receive_zone_packet_for_test(encode(answer));
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, Op::EndLootRequest) == 1; }));
}

TEST_CASE("loot: walking away from the corpse closes the window (CLootWnd::OnProcessFrame)") {
    Rig rig(MockConfig::standard());
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    std::uint32_t corpse = add_corpse(conn, 5.0f);
    conn.loot(corpse);
    conn.receive_zone_packet_for_test(money(1));
    rig.run_for(std::chrono::milliseconds(600));
    CHECK(MockEqServer::count(rig.server.zone_log, Op::EndLootRequest) == 0);
    // Beyond AvatarHeight * 1.5 + 20 = 23 (without z).
    conn.world_for_test().spawns[corpse].position += glm::vec3(30.0f, 0.0f, 0.0f);
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, Op::EndLootRequest) == 1; }));
}

TEST_CASE("loot: a stack on someone else's corpse asks how many (CLootWnd::RequestLootSlot)") {
    Rig rig(MockConfig::standard());
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    std::uint32_t corpse = add_corpse(conn, 5.0f);
    conn.loot(corpse);
    conn.receive_zone_packet_for_test(money(1));
    AppPacket stack = loot_item_packet(23, "Bone Chips");
    {
        ItemPacket ip;
        ip.type = kItemPacketLoot;
        ip.item.id = 13073;
        ip.item.name = "Bone Chips";
        ip.item.slot.slot = 23;
        ip.item.stack_count = 7;
        ip.item.max_stack = 20;
        stack = encode_item_packet(false, ip);
    }
    conn.receive_zone_packet_for_test(stack);
    conn.receive_zone_packet_for_test(loot_item_packet(24, "Rusty Dagger"));
    conn.set_client_option_for_session(game::ClientOption::LootNoDrop, 2); // never asks about no-drop items
    conn.destroy_cursor(); // the mock character holds coin, which stops looting
    rig.run_for(std::chrono::milliseconds(30));
    // A single item goes at once with quantity 0.
    conn.click_slot(ItemSlot::corpse(24));
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, Op::LootItem) == 1; }));
    CHECK_FALSE(conn.quantity_request().has_value());
    // The stack opens the quantity dialog centred, from 7 down; nothing is sent until the answer.
    conn.click_slot(ItemSlot::corpse(23));
    REQUIRE(conn.quantity_request().has_value());
    CHECK(conn.quantity_request()->loot);
    CHECK(conn.quantity_request()->centred);
    CHECK(conn.quantity_request()->max == 7);
    CHECK(conn.quantity_request()->start == 7);
    rig.run_for(std::chrono::milliseconds(30));
    CHECK(MockEqServer::count(rig.server.zone_log, Op::LootItem) == 1);
    conn.finish_quantity(3);
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, Op::LootItem) == 2; }));
    std::optional<LootItem> sent;
    for (const auto& p : rig.server.zone_log)
        if (is_op(p, Op::LootItem)) sent = decode_loot_item(p);
    REQUIRE(sent);
    CHECK(sent->slot == 23);
    CHECK(sent->auto_loot == 0);
    CHECK(sent->unknown16 == 3);
    // Ctrl takes one at once.
    conn.click_slot(ItemSlot::corpse(23), false, true);
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, Op::LootItem) == 3; }));
    for (const auto& p : rig.server.zone_log)
        if (is_op(p, Op::LootItem)) sent = decode_loot_item(p);
    CHECK(sent->unknown16 == 1);
}

TEST_CASE("item options: RequestLootSlot's quantity question") {
    CHECK(game::loot_asks_quantity(false, 2));
    CHECK_FALSE(game::loot_asks_quantity(false, 1));
    CHECK_FALSE(game::loot_asks_quantity(true, 20)); // the player's own corpse
}

TEST_CASE("trade: an answer that comes while busy cancels the trade (Handle_OP_TradeRequestAck)") {
    Rig rig(MockConfig::standard());
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    auto& w = conn.world_for_test();
    const game::Spawn* me = w.player();
    REQUIRE(me);
    game::Spawn pc;
    pc.id = 4100;
    pc.name = "Partner";
    pc.position = me->position + glm::vec3(3.0f, 0.0f, 0.0f);
    pc.heights.avatar_height = 6.0f;
    w.spawns[pc.id] = pc;
    conn.destroy_cursor();
    conn.give_to(pc.id);
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, Op::TradeRequest) == 1; }));
    // A request still waiting for its answer does not make the client busy; a loot window does.
    std::uint32_t corpse = add_corpse(conn, 5.0f);
    conn.loot(corpse);
    conn.receive_zone_packet_for_test(money(1));
    REQUIRE(conn.loot_session().has_value());
    conn.receive_zone_packet_for_test(encode_trade_request_ack({w.player_id, pc.id}));
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, Op::CancelTrade) == 1; }));
    CHECK(MockEqServer::count(rig.server.zone_log, Op::ChannelMessage) >= 1);
    CHECK_FALSE(conn.trade().has_value());
}
