// The stats window's player (app/stats_ui.h) reads its connection's spell table: it must not outlive the connection.
// A crash report: a link death in game, character select, the zone again, and the first frame's fog read the
// player's TotalEffect through the destroyed connection's spell table.
#include <doctest/doctest.h>

#include <functional>

#include "app/stats_ui.h"
#include "mock_server/harness.h"

using namespace nmsc;
using namespace nmsc::test;

TEST_CASE("Stats window: the player it built goes when the connection leaves the zone or is forgotten") {
    EmptyGameDir dir;
    GameDir game(dir.path());
    MockEqServer server(MockConfig::standard());
    net::GameConnection conn(game, quick_options());
    auto until = [&](const std::function<bool()>& done) { return pump([&] { conn.update(); server.poll(); }, done); };
    conn.start(server.ticket());
    REQUIRE(until([&] { return conn.state() == net::ConnectionState::CharacterSelect; }));
    conn.enter_world("Gennro");
    REQUIRE(until([&] { return conn.state() == net::ConnectionState::InZone; }));

    app::StatsUi stats(game); // closed: draw() builds the player without drawing a window
    stats.draw(conn);
    REQUIRE(stats.character() != nullptr);
    stats.forget();
    CHECK(stats.character() == nullptr);

    stats.draw(conn);
    REQUIRE(stats.character() != nullptr);
    conn.disconnect();
    REQUIRE(conn.state() != net::ConnectionState::InZone);
    stats.draw(conn);
    CHECK(stats.character() == nullptr);
}
