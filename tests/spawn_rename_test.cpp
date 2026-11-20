// OP_GMLastName and OP_LevelAppearance in EQEmu's layouts (GMLastName_Struct,
// LevelAppearance_Struct) against the original's handlers (0x4bbcd0, 0x4d0053 / 0x58fed0).

#include <doctest/doctest.h>

#include "mock_server/harness.h"
#include "net/rename_packets.h"

using namespace hailstone;
using namespace hailstone::net;
using namespace hailstone::test;

namespace {

constexpr std::uint32_t kSpawn = 301;

net::AppPacket truncated(net::AppPacket p) {
    p.data.pop_back();
    return p;
}

LevelAppearance eqemu_level_up(std::uint32_t id) {
    // Mob::SendLevelAppearance: parm 0x4d..0x51 with the values it fills in.
    LevelAppearance v;
    v.spawn_id = id;
    v.groups = {{{0x4d, 1, 0}, {0x4e, 2, 0}, {0x4f, 1, 1}, {0x50, 1, 1}, {0x51, 2, 0}}};
    return v;
}

} // namespace

TEST_CASE("GMLastName: wire layout and decode") {
    auto p = encode(GMLastName{"Orc_pawn", "Gm_name", "the Smith", 1});
    CHECK(p.data.size() == kGMLastNameSize);
    auto back = decode_gm_last_name(p);
    REQUIRE(back);
    CHECK(back->name == "Orc_pawn");
    CHECK(back->gm_name == "Gm_name");
    CHECK(back->last_name == "the Smith");
    CHECK(back->apply == 1);
    CHECK_FALSE(decode_gm_last_name(truncated(p)));
}

TEST_CASE("GMLastName sets the last name of a spawn and of the player") {
    Rig rig;
    REQUIRE(rig.to_zone());
    rig.conn.receive_zone_packet_for_test(net::encode(make_spawn("Orc_pawn", kSpawn, 1, 54, 0, 0, 3)));

    rig.conn.receive_zone_packet_for_test(encode(GMLastName{"Orc_pawn", "Gm", "Flag_zero", 0}));
    CHECK(rig.conn.world().spawns.at(kSpawn).last_name.empty());
    rig.conn.receive_zone_packet_for_test(encode(GMLastName{"Unknown_one", "Gm", "Nope", 1}));
    rig.conn.receive_zone_packet_for_test(truncated(encode(GMLastName{"Orc_pawn", "Gm", "Short", 1})));
    CHECK(rig.conn.world().spawns.at(kSpawn).last_name.empty());

    const auto serial = rig.conn.world().spawn_set_serial;
    rig.conn.receive_zone_packet_for_test(encode(GMLastName{"Orc_pawn", "Gm", "the Smith", 1}));
    CHECK(rig.conn.world().spawns.at(kSpawn).last_name == "the Smith");
    CHECK(rig.conn.world().spawn_set_serial != serial);

    rig.conn.receive_zone_packet_for_test(encode(GMLastName{rig.conn.world().player()->name, "Gm", "Own_last", 1}));
    CHECK(rig.conn.world().player()->last_name == "Own_last");
    REQUIRE(rig.conn.profile());
    CHECK(rig.conn.profile()->last_name == "Own_last");
}

TEST_CASE("LevelAppearance: wire layout and decode") {
    auto p = encode(eqemu_level_up(kSpawn));
    CHECK(p.data.size() == kLevelAppearanceSize);
    auto back = decode_level_appearance(p);
    REQUIRE(back);
    CHECK(back->spawn_id == kSpawn);
    CHECK(back->groups[0].effect_id == 0x4d);
    CHECK(back->groups[4].effect_id == 0x51);
    CHECK(back->groups[2].slot == 1);
    CHECK(back->groups[2].flag == 1);
    CHECK_FALSE(decode_level_appearance(truncated(p)));

    LevelAppearance sparse;
    sparse.groups[1].effect_id = 0x4e;
    CHECK_FALSE(level_appearance_group_used(sparse.groups[0]));
    CHECK(level_appearance_group_used(sparse.groups[1]));
    LevelAppearanceGroup negative;
    negative.effect_id = 0xffffffffu; // the original's test is a signed "above 0"
    CHECK_FALSE(level_appearance_group_used(negative));
}

TEST_CASE("LevelAppearance is kept for a known spawn only") {
    Rig rig;
    REQUIRE(rig.to_zone());
    rig.conn.receive_zone_packet_for_test(net::encode(make_spawn("Orc_pawn", kSpawn, 1, 54, 0, 0, 3)));
    rig.conn.receive_zone_packet_for_test(encode(eqemu_level_up(9999))); // unknown spawn
    CHECK(rig.conn.level_appearances().empty());
    rig.conn.receive_zone_packet_for_test(truncated(encode(eqemu_level_up(kSpawn))));
    CHECK(rig.conn.level_appearances().empty());
    rig.conn.receive_zone_packet_for_test(encode(eqemu_level_up(kSpawn)));
    REQUIRE(rig.conn.level_appearances().size() == 1);
    CHECK(rig.conn.level_appearances().back().spawn_id == kSpawn);
}

namespace {

// Hand-built bytes, not the encoder's: a field of `n` bytes filled by `text` without a terminator when it fills it.
void put_field(std::vector<std::uint8_t>& b, std::size_t at, const std::string& text) {
    for (std::size_t i = 0; i < text.size(); ++i) b[at + i] = static_cast<std::uint8_t>(text[i]);
}

net::AppPacket raw(Op op, const std::vector<std::uint8_t>& b) {
    std::vector<std::byte> d;
    for (auto v : b) d.push_back(static_cast<std::byte>(v));
    return net::AppPacket(to_wire(op), std::move(d));
}

constexpr std::size_t kFlagAt = 196;

} // namespace

TEST_CASE("GMLastName from raw bytes: the flag at 196") {
    std::vector<std::uint8_t> b(200, 0);
    put_field(b, 0, "Orc_pawn");
    put_field(b, 128, "the Smith");
    b[kFlagAt] = 1;
    auto v = decode_gm_last_name(raw(Op::GMLastName, b));
    REQUIRE(v);
    CHECK(v->last_name == "the Smith");
    CHECK(v->apply == 1);
    b[kFlagAt] = 0;
    CHECK(decode_gm_last_name(raw(Op::GMLastName, b))->apply == 0);
}

TEST_CASE("LevelAppearance keeps at most 16 effects") {
    Rig rig;
    REQUIRE(rig.to_zone());
    rig.conn.receive_zone_packet_for_test(net::encode(make_spawn("Orc_pawn", kSpawn, 1, 54, 0, 0, 3)));
    for (std::uint32_t i = 0; i < 40; ++i) {
        auto v = eqemu_level_up(kSpawn);
        v.groups[0].slot = i;
        rig.conn.receive_zone_packet_for_test(encode(v));
    }
    REQUIRE(rig.conn.level_appearances().size() == 16);
    CHECK(rig.conn.level_appearances().front().groups[0].slot == 24);
    CHECK(rig.conn.level_appearances().back().groups[0].slot == 39);
}
