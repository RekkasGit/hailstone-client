// Labels and gauges by EQType (app/lua_eq_values.cpp: __GetLabelFromEQ @0x763640, __GetGaugeValueFromEQ @0x762410)
// fed by packets: the combat timer, the aggro meter's percents and colours, the gauge's special values and the buff
// windows' fade (game/buff_overlays.h).
#include <doctest/doctest.h>

#include <vector>

#include "app/lua_eq.h"
#include "app/lua_eq_values.h"
#include "app/lua_eq_windows.h"
#include "game/buff_overlays.h"
#include "game_files.h"
#include "net/aggro_packets.h"
#include "net/game_connection.h"
#include "net/opcodes.h"

using namespace hailstone;

namespace {

struct Host {
    net::GameConnection conn;
    app::EqHost host;
    explicit Host(const GameDir& game) : conn(game, {}) {
        host.in_game = true;
        host.game = &game;
        host.conn = &conn;
    }
};

// OP_RestState: u8 in combat, then out of combat the downtime in seconds.
net::AppPacket rest_state(bool combat, std::uint32_t seconds) {
    std::vector<std::byte> b{std::byte{combat ? std::uint8_t{1} : std::uint8_t{0}}};
    if (!combat)
        for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::byte>((seconds >> (8 * i)) & 0xff));
    return net::AppPacket(net::to_wire(net::Op::RestState), std::move(b));
}

} // namespace

TEST_CASE("Gauge fill: CGaugeWnd::Draw's special values") {
    CHECK(app::gauge_fill(0).fill == 0);
    CHECK(app::gauge_fill(640).fill == 640);
    CHECK_FALSE(app::gauge_fill(640).tint);
    CHECK(app::gauge_fill(-1).fill < 0); // nothing
    CHECK(app::gauge_fill(-3).fill < 0); // a corpse: empty
    CHECK(app::gauge_fill(-9).fill < 0);
    auto red = app::gauge_fill(-2); // full, dark red
    CHECK(red.fill == 1000);
    CHECK(red.tint == 0xff800000u);
    CHECK(app::gauge_fill(-110).fill == 0); // the purple band: (v + 110) * 10
    CHECK(app::gauge_fill(-60).fill == 500);
    CHECK(app::gauge_fill(-10).fill == 1000);
    CHECK(app::gauge_fill(-60).tint == 0xff800080u);
    CHECK(app::gauge_fill(-2000).fill == 0); // the pink band: v + 2000
    CHECK(app::gauge_fill(-1250).fill == 750);
    CHECK(app::gauge_fill(-1000).tint == 0xffc08080u);
    CHECK(app::gauge_fill(-500).fill < 0); // between the bands: empty
}

TEST_CASE("Buff fade: under 19 s, once a second; a bard's short song keeps its alpha") {
    CHECK(game::buff_fade(19000, 123456, false) == 1.0); // 19 s left: not yet
    CHECK(game::buff_fade(18999, 5000, false) == 1.0);   // the second's start
    CHECK(game::buff_fade(18999, 5500, false) == doctest::Approx(0.5));
    CHECK(game::buff_fade(1, 5999, false) == doctest::Approx(0.01));
    CHECK(game::buff_fade(0, 5500, false) == 1.0);  // run out
    CHECK(game::buff_fade(-1, 5500, false) == 1.0); // permanent
    CHECK(game::buff_fade(5000, 5500, true) == 1.0);
}

TEST_CASE("Combat timer: label 137 and gauge 29 from OP_RestState") {
    const GameDir* game = test::game_dir();
    if (!game) return;
    Host t(*game);
    CHECK(app::eq_label(t.host, 137).text.empty());
    CHECK(app::eq_gauge(t.host, 29).value == -1);
    t.conn.receive_zone_packet_for_test(rest_state(false, 75));
    auto text = app::eq_label(t.host, 137).text;
    CHECK((text == "01:15" || text == "01:14")); // "%02d:%02d" of the seconds left
    CHECK(app::eq_gauge(t.host, 29).value == 1000); // min(left, 30) * 1000 / 30
    t.conn.receive_zone_packet_for_test(rest_state(false, 15));
    int v = app::eq_gauge(t.host, 29).value;
    CHECK((v == 500 || v == 466));
    t.conn.receive_zone_packet_for_test(rest_state(true, 0)); // in combat: no downtime
    CHECK(app::eq_label(t.host, 137).text.empty());
    CHECK(app::eq_gauge(t.host, 29).value == -1);
}

TEST_CASE("Aggro meter labels: percents, their colours and the lock") {
    const GameDir* game = test::game_dir();
    if (!game) return;
    Host t(*game);
    net::AggroTargetInfo info{0, 77};
    t.conn.receive_zone_packet_for_test(net::encode(info));
    net::AggroUpdate u;
    u.entries = {{0, 80}, {1, 100}, {2, 0}, {8, 40}};
    t.conn.receive_zone_packet_for_test(net::encode(u));
    auto mine = app::eq_label(t.host, 305);
    CHECK(mine.text == "80");
    REQUIRE(mine.argb);
    CHECK(*mine.argb == (0xff000000u | (app::user_color_rgb(0x61) ? app::user_color_rgb(0x61) : 0xf0f0f0u)));
    CHECK(app::eq_label(t.host, 306).text == "80");
    CHECK(app::eq_label(t.host, 307).text == "100");
    CHECK(app::eq_label(t.host, 315).text == "40"); // extended target slot 1: entry 8
    CHECK(app::eq_label(t.host, 314).text.empty());  // below 1: empty
    CHECK(app::eq_gauge(t.host, 149).value == 800);
    // locked on another spawn: the player window's copies (304, 306, 308) go blank
    info.lock_id = 55;
    t.conn.receive_zone_packet_for_test(net::encode(info));
    CHECK(app::eq_label(t.host, 306).text.empty());
    CHECK(app::eq_label(t.host, 308).text.empty());
    CHECK(app::eq_label(t.host, 305).text == "80");
}
