// An adventure recruiter's Request (net/game_connection_adventure.cpp): stock EQEmu drops one from a player in no
// group or raid unanswered, so the client stops it with its own line; a server extension's solo_adventure switch
// (game/server_ext.h) lets it go out.
#include <doctest/doctest.h>

#include <memory>

#include "game/coords.h"
#include "game/server_ext.h"
#include "mq_test_world.h"

using namespace nmsc;

namespace {

constexpr const char* kRefusal = "You must be in a group or raid to request an adventure.";

struct Solo final : game::ServerExtension {
    bool on = false;
    bool solo_adventure(const net::GameConnection&) const override { return on; }
};

// The test world with the group gone and a recruiter (class 60) 10 away.
void solo_at_recruiter(mq_test::World& w) {
    using A = net::GameConnectionTestAccess;
    A::group(*w.conn) = {};
    w.add(20, "Mannis_McGuyett000", true, 65, game::server_to_world({0, 10, 0}));
    A::world(*w.conn).spawns[20].class_id = 60;
    REQUIRE(w.conn->open_adventure_recruiter(20));
}

} // namespace

TEST_CASE("adventure: a solo request is stopped unless a server extension takes solo adventures") {
    auto ext = std::make_shared<Solo>();
    net::ConnectionOptions opts;
    opts.plugins = ext;
    mq_test::World w("nmsc_adventure_request_test", opts);
    solo_at_recruiter(w);

    w.conn->adventure_request(2, 1);
    CHECK(w.conn->adventure().npc_text == kRefusal);

    ext->on = true;
    REQUIRE(w.conn->open_adventure_recruiter(20)); // clears the text
    w.conn->adventure_request(2, 1);
    CHECK(w.conn->adventure().npc_text.empty()); // sent (no zone session here), not refused
}

TEST_CASE("adventure: a grouped request goes out without the switch") {
    mq_test::World w("nmsc_adventure_request_test");
    w.add(20, "Mannis_McGuyett000", true, 65, game::server_to_world({0, 10, 0}));
    net::GameConnectionTestAccess::world(*w.conn).spawns[20].class_id = 60;
    REQUIRE(w.conn->open_adventure_recruiter(20)); // the test world's player is in a group
    w.conn->adventure_request(1, 0);
    CHECK(w.conn->adventure().npc_text.empty());
}
