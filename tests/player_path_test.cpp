#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "core/ini_file.h"
#include "game/player_path.h"

using namespace nmsc;
using namespace nmsc::game;

namespace {

// The stock SpellEffects\Path.ini's [Path] numbers that shape the points.
PathSettings stock() {
    IniFile ini;
    ini.parse("[Path]\nControlSegmentLength=10\nMergeDistance=15\nSegmentLength=2\nLODFactor=0\nFloorOffset=1\n"
              "OpaqueStart=5\nOpaqueEnd=15\nFadeStart=120\nFadeEnd=200\nFinishRange=10\nInitialLength=25\n"
              "GrowSpeed=0.1\nAlpha=180\nCurveScale=10\nMaxEmitterCount=10\nEmitterSpeed=0.02\nEmitterPeriod=2000\n"
              "EmitterStartCoordinate=0\nEmitter0=E0\n[E0]\nDefinition=1157\nLifeSpan=5000\n");
    return load_path_settings(&ini);
}

} // namespace

TEST_CASE("Path.ini: PlayerPath_LoadSettings' keys, defaults and orderings") {
    PathSettings d = load_path_settings(nullptr);
    CHECK(d.segment_length == 5.0f);
    CHECK(d.merge_distance == 5.0f);
    CHECK(d.finish_range == 10.0f);
    CHECK(d.fade_start == 80.0f);
    CHECK(d.fade_end == 110.0f);
    CHECK(d.alpha == 0x80);
    CHECK(d.emitters[0].definition == -1);
    CHECK(d.emitters[0].life_span == 10);
    IniFile ini;
    ini.parse("[Path]\nFadeStart=200\nFadeEnd=120\nMaxEmitterCount=99\nAdditive=0\nSineOffsetX=1\nSineOffsetY=2\n"
              "SineOffsetZ=3\nFinishEmitter2=F\n[F]\nDefinition=1193\nLifeSpan=250.7\nDensity=0.5\n");
    PathSettings s = load_path_settings(&ini);
    CHECK(s.fade_start == 120.0f); // swapped into order
    CHECK(s.fade_end == 200.0f);
    CHECK(s.max_emitters == 20);
    CHECK_FALSE(s.additive);
    CHECK(s.sine_offset == glm::vec3(1, 3, 3)); // SineOffsetY is never read
    CHECK(s.finish_emitters[2].definition == 1193);
    CHECK(s.finish_emitters[2].life_span == 250);
    CHECK(s.finish_emitters[2].density == 0.5f);
    CHECK(stock().emitters[0].definition == 1157);
}

TEST_CASE("PlayerPath: the finds' request fields (Send_OP_FindPersonRequest)") {
    PlayerPath p;
    FindRequest r = p.find_spawn(0x1234, true);
    CHECK(r.kind == FindKind::Spawn);
    CHECK(r.id == 0x1234);
    CHECK(r.item_a == -1);
    CHECK(r.item_b == -1);
    CHECK(r.point == glm::vec3(0));
    r = p.find_map_location({10, 20, 3}, true);
    CHECK(r.kind == FindKind::MapLocation);
    CHECK(r.id == 0xffff);
    CHECK(r.point == glm::vec3(10, 20, 3));
    r = p.find_plot(7, false);
    CHECK(r.kind == FindKind::RealEstatePlot);
    CHECK(r.item_a == 7);
    CHECK(r.item_b == -1);
    CHECK_FALSE(p.active());
}

TEST_CASE("PlayerPath: a reply of fewer than two points is a failed find") {
    PlayerPath p;
    p.update({0, 0, 0}, 0, 7); // the zone the frames run in
    p.set_settings(stock(), 6.0f);
    p.find_map_location({1, 2, 3}, true);
    auto none = p.handle_reply({}, 100, false, {});
    CHECK(none.failed);
    CHECK_FALSE(none.failed_point);
    std::vector<glm::vec3> one = {{50, 60, 2}};
    auto r = p.handle_reply(one, 100, false, {});
    CHECK(r.failed);
    REQUIRE(r.failed_point);
    CHECK(*r.failed_point == glm::vec3(50, 60, 2));
    CHECK(r.failed_kind == FindKind::MapLocation);
    std::vector<glm::vec3> zero = {{0, 0, 0}};
    CHECK_FALSE(p.handle_reply(zero, 100, false, {}).failed_point);
    CHECK_FALSE(p.active());
}

TEST_CASE("PlayerPath: the reply is walked from its end, merged and filled (HandleFindPersonReply)") {
    PlayerPath p;
    p.set_settings(stock(), 6.0f); // FloorOffset 1 x AvatarHeight 6
    p.find_spawn(5, true);
    // EQEmu's reply: the destination, the path from the player (0, 0) to it, the destination again.
    std::vector<glm::vec3> reply = {{100, 0, 0}, {0, 0, 0}, {40, 0, 0}, {45, 0, 0}, {100, 0, 0}, {100, 0, 0}};
    auto floor = [](const glm::vec3&, float) -> std::optional<float> { return -2.0f; };
    auto r = p.handle_reply(reply, 1000, false, floor);
    CHECK(r.started);
    CHECK_FALSE(r.failed);
    REQUIRE(r.face);
    CHECK(*r.face == glm::vec3((0 + 40 + 45) / 3.0f, 0, 0)); // the mean of points 1..3
    CHECK(p.active());
    CHECK(p.destination() == glm::vec3(100, 0, 0));
    const auto& pts = p.points();
    // 100 (the duplicate dropped as closer than 15); 45 is 55 away: points every 10 while t < 1 - 10/55, then 45;
    // 40 merged; 0 is 45 away: every 10 while t < 1 - 10/45, then 0.
    const float want[] = {100, 90, 80, 70, 60, 45, 35, 25, 15, 0};
    REQUIRE(pts.size() == 10);
    for (std::size_t i = 0; i < pts.size(); ++i) {
        CHECK(pts[i].x == doctest::Approx(want[i]));
        CHECK(pts[i].z == 4.0f); // on the floor (-2) plus FloorOffset x AvatarHeight (6)
    }
    // A GM's points stay where the server put them.
    PlayerPath gm;
    gm.set_settings(stock(), 6.0f);
    gm.find_spawn(5, false);
    auto g = gm.handle_reply(reply, 1000, true, floor);
    CHECK_FALSE(g.face);
    CHECK(gm.points().front().z == 0.0f);
}

TEST_CASE("PlayerPath: the update trims, follows the player and ends at FinishRange") {
    PlayerPath p;
    p.update({0, 0, 0}, 0, 7); // the zone the frames run in
    p.set_settings(stock(), 6.0f);
    p.find_spawn(5, false);
    std::vector<glm::vec3> reply = {{200, 0, 0}, {0, 0, 0}, {100, 0, 0}, {200, 0, 0}, {200, 0, 0}};
    p.handle_reply(reply, 1000, true, {});
    REQUIRE(p.active());
    std::size_t before = p.points().size();
    // The first update: the trim point is the lowest point within OpaqueStart (5) of the player.
    auto u = p.update({0, 0, 0}, 1600, 7);
    CHECK_FALSE(u.arrived);
    CHECK(p.points().back() == glm::vec3(0, 0, 0));
    CHECK(p.points().size() <= before);
    CHECK(p.length() > 190.0f);
    // Walk along: the path follows the player's end.
    p.update({50, 0, 0}, 2200, 7);
    CHECK(p.points().back() == glm::vec3(50, 0, 0));
    for (std::size_t i = 1; i < p.points().size(); ++i) CHECK(p.points()[i].x <= p.points()[i - 1].x + 0.001f);
    CHECK(p.active());
    // Within FinishRange (10) of the destination: arrived once.
    u = p.update({195, 0, 0}, 2800, 7);
    CHECK(u.arrived);
    CHECK_FALSE(p.active());
    CHECK(p.finished());
    CHECK(p.update({195, 0, 0}, 2900, 7).arrived == false);
}

TEST_CASE("PlayerPath: a map location's destination takes the player's height; a new zone ends the path") {
    PlayerPath p;
    p.update({0, 0, 0}, 0, 7); // the zone the frames run in
    p.set_settings(stock(), 6.0f);
    p.find_map_location({300, 0, 0}, false);
    std::vector<glm::vec3> reply = {{300, 0, 0}, {0, 0, 0}, {300, 0, 0}, {300, 0, 0}};
    p.handle_reply(reply, 1000, true, {});
    p.update({0, 0, 50}, 1100, 7);
    CHECK(p.destination().z == 50.0f);
    CHECK(p.active());
    p.update({0, 0, 50}, 1200, 8);
    CHECK_FALSE(p.active());
}

TEST_CASE("PlayerPath: the line fades in near the player and out past the grown length (FUN_0059b040)") {
    PlayerPath p;
    p.update({0, 0, 0}, 0, 7); // the zone the frames run in
    p.set_settings(stock(), 6.0f);
    p.find_spawn(5, false);
    std::vector<glm::vec3> reply = {{400, 0, 0}, {0, 0, 0}, {400, 0, 0}, {400, 0, 0}};
    p.handle_reply(reply, 1000, true, {});
    p.update({0, 0, 0}, 1000, 7);
    // InitialLength 25 - (200 - 120): the visible stretch starts at -55 .. 25.
    CHECK(p.fade_near() == doctest::Approx(-55.0f));
    CHECK(p.fade_far() == doctest::Approx(25.0f));
    CHECK(p.line_alpha(0.0f) == 0);      // inside OpaqueStart
    CHECK(p.line_alpha(15.0f) > 0);      // past OpaqueEnd, before the fade's end
    CHECK(p.line_alpha(30.0f) == 0);     // past the grown length
    REQUIRE_FALSE(p.line().empty());
    CHECK(p.line().front().coordinate == 0.0f);
    // It grows at GrowSpeed per ms up to FadeStart .. FadeEnd.
    p.update({0, 0, 0}, 3000, 7);
    CHECK(p.fade_near() == doctest::Approx(120.0f));
    CHECK(p.fade_far() == doctest::Approx(200.0f));
}
