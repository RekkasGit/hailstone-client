// The bandolier's swap (GameConnection::bandolier_activate, PcZoneClient::BandolierSwap @0x585aa0) against the mock
// server: it moves the items the way EQEmu's Client::SetBandolier does, and is all or nothing like the original's
// MultipleItemMoveManager::ProcessMove. A weapon that has no room to go back to cancels the whole swap with eqstr
// 9342 and no OP_Bandolier, so the client never loses an item the server still has (a player's bug report: with full
// bags, swapping sets and back emptied the primary slot until the next login).
#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "game/inventory.h"
#include "mock_server/harness.h"
#include "net/packet_io.h"
#include "net/player_packets.h"

using namespace hailstone;
using namespace hailstone::test;
using net::ItemSlot;

namespace {

int swaps_sent(const MockEqServer& server, std::uint8_t set) {
    return static_cast<int>(std::count_if(server.zone_log.begin(), server.zone_log.end(), [&](const net::AppPacket& p) {
        if (!net::is_op(p, net::Op::Bandolier)) return false;
        auto b = net::decode_bandolier(p);
        return b && b->action == net::kBandolierSet && b->number == set;
    }));
}

std::uint32_t id_at(const net::GameConnection& conn, std::int16_t slot) {
    const net::Item* it = conn.inventory().at(ItemSlot::possessions(slot));
    return it ? it->id : 0;
}

std::vector<std::string> chat_lines(net::GameConnection& conn) {
    std::vector<std::string> out;
    while (auto e = conn.poll_chat()) out.push_back(e->text);
    return out;
}

} // namespace

TEST_CASE("bandolier swap: a weapon with no room to go back cancels the swap; with room it goes through") {
    MockConfig config = MockConfig::standard();
    auto& inv = config.characters[0].inventory;
    inv.push_back(make_item(1001, "Cudgel", 13));
    for (std::int16_t s = net::kSlotGeneral1; s <= net::kSlotGeneral10; ++s)
        inv.push_back(make_item(2000u + static_cast<std::uint32_t>(s), "Rock", s));
    Rig rig(std::move(config));
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    REQUIRE(conn.bandolier_sets() >= 2);

    // Set 0 "1h" holds the Cudgel; set 1 "nodmg" a Dagger, made while the server had it in the primary slot.
    REQUIRE(conn.bandolier_create(0, "1h"));
    rig.server.item_packet(net::kItemPacketCharInventory, make_item(1002, "Dagger", 13));
    REQUIRE(rig.until([&] { return id_at(conn, 13) == 1002; }));
    REQUIRE(conn.bandolier_create(1, "nodmg"));
    rig.server.item_packet(net::kItemPacketCharInventory, make_item(1001, "Cudgel", 13));
    REQUIRE(rig.until([&] { return id_at(conn, 13) == 1001; }));
    chat_lines(conn);

    // No Dagger anywhere, the bags full: the Cudgel would have to go back to the inventory and cannot, so nothing
    // moves and nothing is sent (the original's error 0x11).
    const std::uint64_t revision = conn.inventory().revision();
    CHECK_FALSE(conn.bandolier_activate(1));
    CHECK(id_at(conn, 13) == 1001);
    for (std::int16_t s = net::kSlotGeneral1; s <= net::kSlotGeneral10; ++s)
        CHECK(id_at(conn, s) == 2000u + static_cast<std::uint32_t>(s));
    CHECK(conn.inventory().revision() == revision);
    auto lines = chat_lines(conn);
    CHECK(std::find(lines.begin(), lines.end(), "NO ROOM FOR THE BANDOLIER") != lines.end());
    rig.run_for(std::chrono::milliseconds(30));
    CHECK(swaps_sent(rig.server, 1) == 0);

    // The Dagger in the last general slot: pulling it makes the room the Cudgel needs, so both swaps go through and
    // the two weapons trade places each time.
    rig.server.item_packet(net::kItemPacketCharInventory, make_item(1002, "Dagger", net::kSlotGeneral10));
    REQUIRE(rig.until([&] { return id_at(conn, net::kSlotGeneral10) == 1002; }));
    REQUIRE(conn.bandolier_activate(1));
    CHECK(id_at(conn, 13) == 1002);
    CHECK(id_at(conn, net::kSlotGeneral10) == 1001);
    REQUIRE(conn.bandolier_activate(0));
    CHECK(id_at(conn, 13) == 1001);
    CHECK(id_at(conn, net::kSlotGeneral10) == 1002);
    REQUIRE(rig.until([&] { return swaps_sent(rig.server, 1) == 1 && swaps_sent(rig.server, 0) == 1; }));
}
