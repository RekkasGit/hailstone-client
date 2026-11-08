// The cast stage a spell landing without an OP_BeginCast (an item, weapon or augment proc) gets at its source, as a
// cast's (GameConnection::handle_spell, OP_Action 231).

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "game/spells.h"
#include "mock_server/harness.h"
#include "net/spell_packets.h"

using namespace hailstone;

namespace {

constexpr std::uint32_t kNuke = 21, kSong = 22;

std::string spell_line(int id, int skill) {
    std::vector<std::string> fl(237, "0");
    fl[0] = std::to_string(id);
    fl[1] = "Spell" + std::to_string(id);
    for (int k = 0; k < 12; ++k) fl[86 + static_cast<std::size_t>(k)] = "254";
    fl[100] = std::to_string(skill);
    fl[145] = "1"; // spell_anim
    std::string out;
    for (std::size_t i = 0; i < fl.size(); ++i) out += (i ? "^" : "") + fl[i];
    return out + "\n";
}

net::AppPacket landing(std::uint32_t spell, std::uint16_t source, std::uint16_t target, std::uint8_t flag) {
    net::Action a;
    a.source = source;
    a.target = target;
    a.type = 231;
    a.spell_id = spell;
    a.effect_flag = flag;
    return net::encode(a);
}

int cast_stages(const std::vector<net::SpellEffectCue>& cues, std::uint32_t caster, bool proc) {
    int n = 0;
    for (const auto& c : cues)
        if (c.stage == 0 && c.caster == caster && c.proc == proc) ++n;
    return n;
}

} // namespace

TEST_CASE("A proc's landing plays the cast stage at its source; a cast's landing and a song's pulse do not") {
    test::Rig rig;
    REQUIRE(rig.to_zone());
    auto& conn = rig.conn;
    auto table = std::make_shared<game::SpellTable>();
    REQUIRE(table->parse(spell_line(kNuke, 24) + spell_line(kSong, 41)) == 2);
    conn.set_spell_table_for_test(table);
    const auto me = static_cast<std::uint16_t>(conn.world().player_id);
    REQUIRE(me != 0);
    const std::uint16_t mob = 900;
    (void)conn.take_spell_effect_cues();

    SUBCASE("a weapon proc: one cast stage at the source for the landing's two OP_Actions, then the impacts") {
        conn.receive_zone_packet_for_test(landing(kNuke, me, mob, 0));
        conn.receive_zone_packet_for_test(landing(kNuke, me, mob, 4));
        auto cues = conn.take_spell_effect_cues();
        REQUIRE(cues.size() == 3);
        CHECK(cues[0].stage == 0);
        CHECK(cues[0].proc);
        CHECK(cues[0].caster == me);
        CHECK(cues[0].spell == kNuke);
        CHECK(cues[1].stage == 2);
        CHECK(cues[2].stage == 2);
    }
    SUBCASE("a proc landing on a second target with it shares the cast stage") {
        conn.receive_zone_packet_for_test(landing(kNuke, mob, me, 0));
        conn.receive_zone_packet_for_test(landing(kNuke, mob, mob + 1, 0));
        CHECK(cast_stages(conn.take_spell_effect_cues(), mob, true) == 1);
    }
    SUBCASE("a cast (or a buff's proc, which EQEmu begins with OP_BeginCast) has its cast stage already") {
        net::BeginCast b;
        b.caster_id = mob;
        b.spell_id = kNuke;
        b.cast_time = 2000;
        conn.receive_zone_packet_for_test(net::encode(b));
        conn.receive_zone_packet_for_test(landing(kNuke, mob, me, 0));
        conn.receive_zone_packet_for_test(landing(kNuke, mob, me, 4));
        auto cues = conn.take_spell_effect_cues();
        CHECK(cast_stages(cues, mob, false) == 1);
        CHECK(cast_stages(cues, mob, true) == 0);
    }
    SUBCASE("a bard song's pulse") {
        conn.receive_zone_packet_for_test(landing(kSong, mob, me, 0));
        CHECK(cast_stages(conn.take_spell_effect_cues(), mob, true) == 0);
    }
}
