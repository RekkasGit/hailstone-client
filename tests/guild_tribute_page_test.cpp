// The guild window's Tribute and Banner pages (app/lua_eq_social.cpp GuildWindow) against the mock EQEmu: the
// tribute list from OP_GuildMemberList, a member's row updated by OP_GuildOptInOut, the opt-out buttons as
// CGuildMgmtWnd::UpdateButtons enables them and what they send, and the banner page without banner messages.
#include <doctest/doctest.h>

#include <algorithm>
#include <functional>
#include <string>

#include <imgui.h>
#include <lua.hpp>

#include "app/lua_eq.h"
#include "core/byte_io.h"
#include "mock_server/harness.h"
#include "net/packet_io.h"
#include "net/social_packets.h"
#include "net/tribute_packets.h"
#include "net/zone_packets.h"

using namespace hailstone;
using namespace hailstone::test;

namespace {

std::string run(lua_State* L, const std::string& code) {
    if (luaL_dostring(L, code.c_str()) != LUA_OK) {
        std::string e = lua_tostring(L, -1);
        lua_settop(L, 0);
        return "error: " + e;
    }
    std::string out = lua_isnil(L, -1) ? "nil" : luaL_tolstring(L, -1, nullptr);
    lua_settop(L, 0);
    return out;
}

net::GuildMember member(const char* name, std::uint16_t zone, std::uint32_t donations) {
    net::GuildMember m;
    m.name = name;
    m.level = 50;
    m.rank = 5;
    m.zone_id = zone;
    m.tribute_enable = 1;
    m.total_tribute = donations;
    return m;
}

} // namespace

TEST_CASE("mock server: the guild window's Tribute page lists the members' tribute and opts a member out") {
    ImGuiContext* ctx = ImGui::CreateContext();
    {
        EmptyGameDir dir;
        GameDir game(dir.path());
        MockEqServer server(MockConfig::standard());
        net::GameConnection conn(game, quick_options());
        auto until = [&](const std::function<bool()>& done) { return pump([&] { conn.update(); server.poll(); }, done); };
        conn.start(server.ticket());
        REQUIRE(until([&] { return conn.state() == net::ConnectionState::CharacterSelect; }));
        conn.enter_world("Gennro");
        REQUIRE(until([&] { return conn.state() == net::ConnectionState::InZone && conn.player_status().max_hp != 0; }));

        // Guild 3 (SpawnAppearance 22 for us), then the roster: us online, Alice offline (rank 5 both, no
        // permissions sent, so no permission 22 to change others).
        server.zone.send_reliable(net::encode(net::SpawnAppearance{0, 22, 3}));
        net::GuildMemberList l;
        l.player_name = "Gennro";
        l.guild_id = 3;
        l.members = {member("Gennro", 2, 500), member("Alice", 0, 40)};
        l.members[1].last_tribute = 1700000000;
        l.members[1].tribute_flag = 0;
        server.zone.send_reliable(net::encode(l));
        REQUIRE(until([&] { return conn.guild().in_guild() && conn.guild().members.size() == 2; }));

        app::EqHost host;
        host.in_game = host.ui_ready = true;
        host.conn = &conn;
        app::EqApi api(host);
        lua_State* L = luaL_newstate();
        luaL_openlibs(L);
        lua_newtable(L);
        api.register_functions(L);
        lua_setglobal(L, "eq");
        const std::string list = "'GuildManagementWnd', 'TributeList'";
        api.begin_frame();
        CHECK(run(L, "return (eq.ListInfo(" + list + "))") == "2");
        // name, tribute state (eqstr 12946 + on; the client's own fallback text here), trophy state, donations, date
        CHECK(run(L, "local r = eq.ListRow(" + list + ", 0) return r[1]..'|'..r[2]..'|'..r[3]..'|'..r[4]..'|'..r[5]") ==
              "Gennro|on|off|500|");
        CHECK(run(L, "return eq.ListRow(" + list + ", 1)[4]") == "40");
        CHECK(run(L, "return #eq.ListRow(" + list + ", 1)[5]") == "8"); // "%02d/%02d/%02d"

        // Nobody selected: the buttons are off. Alice (another member, no permission 22): off. Ourselves: on.
        CHECK(run(L, "return (eq.ChildState('GuildManagementWnd', 'TributeOptOutButton'))") == "false");
        CHECK(run(L, "return eq.ListClick(" + list + ", 1, 'left')") == "true");
        CHECK(run(L, "return (eq.ChildState('GuildManagementWnd', 'TributeOptOutButton'))") == "false");
        CHECK(run(L, "return eq.ListClick(" + list + ", 0, 'left')") == "true");
        CHECK(run(L, "return (eq.ChildState('GuildManagementWnd', 'TributeOptOutButton'))") == "true");
        // EQEmu has no trophy tribute: never on, so its button stays off.
        CHECK(run(L, "return (eq.ChildState('GuildManagementWnd', 'TrophyTributeOptOutButton'))") == "false");
        // The selection is the window's one member: the member list shows it too.
        CHECK(run(L, "return (select(3, eq.ListInfo('GuildManagementWnd', 'MemberList')))") == "0");

        CHECK(run(L, "return eq.ClickChild('GuildManagementWnd', 'TributeOptOutButton', 'left')") == "true");
        auto sent = [&] {
            return std::find_if(server.zone_log.begin(), server.zone_log.end(),
                                [](const net::AppPacket& p) { return net::is_op(p, net::Op::GuildOptInOut); });
        };
        REQUIRE(until([&] { return sent() != server.zone_log.end(); }));
        const net::AppPacket& req = *sent();
        REQUIRE(req.data.size() == 80);
        ByteReader r(req.data);
        CHECK(r.le<std::uint32_t>() == 3); // the guild
        CHECK(r.le<std::uint32_t>() == 0); // out
        CHECK(r.fixed_string(64) == "Gennro");

        // EQEmu's answer (worldserver.cpp ServerOP_GuildTributeOptInToggle): the member off, the request's command
        // echoed (0 here: the cells turn yellow).
        net::GuildTributeOpt reply{"Gennro", false, false, 500, 1700000000, 0};
        server.zone.send_reliable(net::encode(reply));
        REQUIRE(until([&] { return !conn.guild().members[0].tribute_on; }));
        CHECK(conn.guild_tribute().opted_in == false);
        CHECK_FALSE(conn.guild().members[0].tribute_flag);
        api.begin_frame();
        CHECK(run(L, "return eq.ListRow(" + list + ", 0)[2]") == "off");
        CHECK(run(L, "return (eq.ChildState('GuildManagementWnd', 'TributeOptOutButton'))") == "false");
        // An answer naming nobody in the roster changes nothing.
        server.zone.send_reliable(net::encode(net::GuildTributeOpt{"Nobody", true, false, 1, 1, 1}));
        server.zone.send_reliable(net::encode(net::GuildTributeOpt{"alice", false, false, 41, 1, 1}));
        REQUIRE(until([&] { return conn.guild().members[1].donations == 41; })); // by name, case-insensitive
        CHECK(conn.guild().members.size() == 2);

        // The Banner page without banner messages: an empty kit list, "No active banner" (eqstr 636), the
        // controls that need a kit or a banner off, the filter, Show All and Refresh on (and nothing sent).
        CHECK(run(L, "return (eq.ListInfo('GuildManagementWnd', 'BannerKitList'))") == "0");
        CHECK(run(L, "return (eq.WindowText('GuildManagementWnd', 'TimeRemainingValueLabel'))") != "");
        CHECK(run(L, "return (eq.ChildState('GuildManagementWnd', 'PlantBannerButton'))") == "false");
        CHECK(run(L, "return (eq.ChildState('GuildManagementWnd', 'EditBannerButton'))") == "false");
        CHECK(run(L, "return (eq.ChildState('GuildManagementWnd', 'RefreshBannerListButton'))") == "true");
        std::size_t before = server.zone_log.size();
        CHECK(run(L, "return eq.ClickChild('GuildManagementWnd', 'ShowAllPossibleBannersButton', 'left')") == "true");
        CHECK(run(L, "return (select(2, eq.ChildState('GuildManagementWnd', 'ShowAllPossibleBannersButton')))") == "true");
        CHECK(run(L, "return eq.ClickChild('GuildManagementWnd', 'RefreshBannerListButton', 'left')") == "true");
        pump([&] { conn.update(); server.poll(); }, [] { return false; }, std::chrono::milliseconds(300));
        for (std::size_t i = before; i < server.zone_log.size(); ++i) {
            CHECK(server.zone_log[i].opcode != 0x0d92);
            CHECK(server.zone_log[i].opcode != 0x4924);
        }
        lua_close(L);
    }
    ImGui::DestroyContext(ctx);
}
