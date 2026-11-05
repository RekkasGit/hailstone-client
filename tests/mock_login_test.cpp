// LoginClient against the mock server's login script (docs/protocol/login.md), and the hand-off from the login
// ticket to the world server: login, server list, play, then GameConnection to the character list.

#include <doctest/doctest.h>

#include "mock_server/harness.h"
#include "net/login.h"

using namespace nmsc;
using namespace nmsc::net;
using namespace nmsc::test;

namespace {

struct LoginRig {
    MockEqServer server;
    LoginClient client;

    explicit LoginRig(MockConfig config = MockConfig::standard()) : server(std::move(config)) {}
    LoginRig(MockConfig config, LoginClient::Options options) : server(std::move(config)), client(std::move(options)) {}
    // Logged in with the list shown, then Play on the mock's world.
    bool to_play() {
        start("tester", "secret");
        if (!until_state(LoginState::LoggedIn)) return false;
        client.request_server_list();
        if (!until_state(LoginState::ServerList)) return false;
        client.request_play(1);
        return true;
    }

    void step() {
        client.update();
        server.poll();
    }
    bool until(const std::function<bool()>& done, std::chrono::milliseconds limit = 8s) {
        return pump([&] { step(); }, done, limit);
    }
    bool until_state(LoginState s) {
        return until([&] { return client.state() == s || client.state() == LoginState::Failed; }) &&
               client.state() == s;
    }
    void start(const std::string& user, const std::string& password) {
        client.start("127.0.0.1", server.login.port(), user, password);
    }
};

} // namespace

TEST_CASE("mock login: login, server list and play hand over a world ticket") {
    LoginRig rig;
    rig.start("tester", "secret");
    REQUIRE(rig.until_state(LoginState::LoggedIn));
    // The credentials went out DES-encrypted; the mock decrypted them.
    CHECK(rig.server.login_user == "tester");
    CHECK(rig.server.login_password == "secret");
    CHECK(rig.client.login_result().account_id == 77);
    CHECK(rig.client.login_result().session_key == "ABCDEFGHIJ");
    CHECK(rig.client.sequence() == 3); // handshake 2, login 3 (docs/protocol/login.md, Sequence numbers)

    rig.client.request_server_list();
    REQUIRE(rig.until_state(LoginState::ServerList));
    CHECK(rig.client.sequence() == 4);
    REQUIRE(rig.client.servers().size() == 1);
    CHECK(rig.client.servers()[0].name == "Mock EQEmu");
    CHECK(rig.client.servers()[0].address == "127.0.0.1");
    CHECK(rig.client.servers()[0].id == 1);
    CHECK(rig.client.servers()[0].status == 0);

    rig.client.request_play(1);
    REQUIRE(rig.until_state(LoginState::PlayAccepted));
    CHECK(rig.client.sequence() == 5);
    auto ticket = rig.client.ticket();
    REQUIRE(ticket);
    CHECK(ticket->account_id == 77);
    CHECK(ticket->session_key == "ABCDEFGHIJ");
    CHECK(ticket->world_host == "127.0.0.1");
    // The client always connects to world on 9000 (docs/protocol/login.md, Hand-off); the mock's world listens on
    // an ephemeral port, so the test points the ticket at it.
    CHECK(ticket->world_port == kWorldPort);
    CHECK(ticket->server_id == 1);
    CHECK(ticket->server_name == "Mock EQEmu");
    rig.client.close();
}

TEST_CASE("mock login: the ticket gets a GameConnection through world authentication to the character list") {
    LoginRig rig;
    rig.start("tester", "secret");
    REQUIRE(rig.until_state(LoginState::LoggedIn));
    rig.client.request_server_list();
    REQUIRE(rig.until_state(LoginState::ServerList));
    rig.client.request_play(1);
    REQUIRE(rig.until_state(LoginState::PlayAccepted));
    WorldTicket ticket = *rig.client.ticket();
    ticket.world_port = rig.server.world.port();
    rig.client.close();

    EmptyGameDir dir;
    GameDir game(dir.path());
    GameConnection conn(game);
    conn.start(ticket);
    REQUIRE(pump([&] { conn.update(); rig.server.poll(); },
                 [&] { return conn.state() == ConnectionState::CharacterSelect; }));
    // World checked the pair the login server issued (CheckAuth) and sent the list.
    REQUIRE(rig.server.login_infos.size() == 1);
    CHECK(rig.server.login_infos[0].account_id == "77");
    CHECK(rig.server.login_infos[0].session_key == "ABCDEFGHIJ");
    REQUIRE(conn.characters().size() == 1);
    CHECK(conn.characters()[0].name == "Gennro");
}

TEST_CASE("mock login: a refused password keeps the server's error id") {
    LoginRig rig;
    rig.start("tester", "wrong");
    REQUIRE(rig.until([&] { return rig.client.state() == LoginState::Failed; }));
    CHECK(rig.client.error_id() == login_error::BadPassword);
    CHECK(rig.server.login_password == "wrong");
}

TEST_CASE("mock login: a refused play request fails with the server's id") {
    MockConfig config = MockConfig::standard();
    config.play_error = 339; // server full
    LoginRig rig(std::move(config));
    rig.start("tester", "secret");
    REQUIRE(rig.until_state(LoginState::LoggedIn));
    rig.client.request_server_list();
    REQUIRE(rig.until_state(LoginState::ServerList));
    rig.client.request_play(1);
    REQUIRE(rig.until([&] { return rig.client.state() == LoginState::Failed; }));
    CHECK(rig.client.error_id() == 339);
    CHECK_FALSE(rig.client.ticket().has_value());
}

TEST_CASE("mock login: the server dropping the connection after login is error 112") {
    LoginRig rig;
    rig.start("tester", "secret");
    REQUIRE(rig.until_state(LoginState::LoggedIn));
    rig.server.login.send_disconnect();
    REQUIRE(rig.until([&] { return rig.client.state() == LoginState::Failed; }));
    CHECK(rig.client.error_id() == login_error::Terminated);
}

TEST_CASE("mock login: 302 asks whether to remove the logged in character; No keeps the server list") {
    MockConfig config = MockConfig::standard();
    config.play_error = login_error::CharacterOnline;
    LoginRig rig(std::move(config));
    REQUIRE(rig.to_play());
    REQUIRE(rig.until_state(LoginState::KickOffered));
    CHECK(rig.client.play_result().offer_kick());
    CHECK(rig.client.play_result().error_id == 302);
    rig.client.answer_kick(false);
    CHECK(rig.client.state() == LoginState::ServerList);
    CHECK(rig.server.kick_requests == 0);
    // The login is still up: Play again asks again.
    rig.client.request_play(1);
    REQUIRE(rig.until_state(LoginState::KickOffered));
}

TEST_CASE("mock login: Yes sends the kick request (0x16); its successful reply (0x32) plays again") {
    MockConfig config = MockConfig::standard();
    config.play_error = login_error::OfflineTraderOnline; // 114
    config.kick_reply = login_error::NoError;
    LoginRig rig(std::move(config));
    REQUIRE(rig.to_play());
    REQUIRE(rig.until_state(LoginState::KickOffered));
    CHECK(rig.client.sequence() == 5);
    rig.client.answer_kick(true);
    CHECK(rig.client.state() == LoginState::Kicking);
    CHECK(rig.client.sequence() == 6); // the kick request takes the next number
    CHECK(rig.client.notice() == login_error_text(login_error::RemovingPlayer));
    REQUIRE(rig.until_state(LoginState::PlayAccepted));
    CHECK(rig.server.kick_requests == 1);
    CHECK(rig.client.sequence() == 7); // the play request again
    REQUIRE(rig.client.ticket());
    CHECK(rig.client.ticket()->server_id == 1);
}

TEST_CASE("mock login: a refused kick fails with the reply's error") {
    MockConfig config = MockConfig::standard();
    config.play_error = login_error::CharacterOnline;
    config.kick_reply = 102;
    LoginRig rig(std::move(config));
    REQUIRE(rig.to_play());
    REQUIRE(rig.until_state(LoginState::KickOffered));
    rig.client.answer_kick(true);
    REQUIRE(rig.until([&] { return rig.client.state() == LoginState::Failed; }));
    CHECK(rig.client.error_id() == 102);
}

TEST_CASE("mock login: an unanswered kick request (EQEmu's login server) times out with 103") {
    MockConfig config = MockConfig::standard();
    config.play_error = login_error::CharacterOnline;
    LoginClient::Options options;
    options.kick_timeout = 200ms; // the original's 60 s
    LoginRig rig(std::move(config), options);
    REQUIRE(rig.to_play());
    REQUIRE(rig.until_state(LoginState::KickOffered));
    rig.client.answer_kick(true);
    REQUIRE(rig.until([&] { return rig.client.state() == LoginState::Failed; }));
    CHECK(rig.server.kick_requests == 1);
    CHECK(rig.client.error_id() == login_error::Timeout);
}

TEST_CASE("mock login: EQEmu's 111 (account already online) is a plain refusal, no question") {
    MockConfig config = MockConfig::standard();
    config.play_error = 111;
    LoginRig rig(std::move(config));
    REQUIRE(rig.to_play());
    REQUIRE(rig.until([&] { return rig.client.state() == LoginState::Failed; }));
    CHECK(rig.client.error_id() == 111);
    CHECK(rig.server.kick_requests == 0);
}
