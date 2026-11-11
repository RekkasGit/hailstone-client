// The Find window's guide path (app/guide_path.h) and the trail paths are drawn with (game/path_trail.h).
#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <vector>

#include "app/guide_path.h"
#include "game/path_trail.h"

using namespace hailstone;

TEST_CASE("Path trail: evenly spaced along the line, on the floor, faded near the player and far along") {
    const std::vector<glm::vec3> line{{0, 0, 0}, {10, 0, 0}, {10, 30, 0}};
    game::TrailStyle st;
    st.spacing = 1.0f;
    st.near_start = 1.0f;
    st.near_end = 4.0f;
    st.far_start = 1000.0f;
    st.far_end = 2000.0f;
    st.beacon = false;
    st.lift = 0.5f;
    // the floor rises along y: a hill between the two corners
    auto floor = [](const glm::vec3& p) -> std::optional<float> { return p.y * 0.1f; };
    auto t = game::build_trail(line, st, 0.0, floor);
    // 1..40 along: 40 samples (the first faded out entirely is dropped)
    REQUIRE(t.size() >= 38);
    REQUIRE(t.size() <= 40);
    for (std::size_t i = 1; i < t.size(); ++i) {
        float d = glm::length(glm::vec2(t[i].position) - glm::vec2(t[i - 1].position));
        CHECK(d <= 1.0f + 1e-3f); // a corner cuts the step short, nothing is further apart
        CHECK(d >= 0.70f);
    }
    // on the floor, lifted
    const auto& last = t.back();
    CHECK(last.position.z == doctest::Approx(last.position.y * 0.1f + 0.5f));
    // dim at the player, bright further on
    CHECK(t.front().alpha < t[10].alpha + 0.5f);
    CHECK(t.front().alpha < 0.5f * st.alpha * (1.0f + st.band_boost));

    // a floor far off the line (a gorge under a bridge) is not used
    auto deep = [](const glm::vec3&) -> std::optional<float> { return -50.0f; };
    auto b = game::build_trail(line, st, 0.0, deep);
    REQUIRE_FALSE(b.empty());
    CHECK(b.back().position.z == doctest::Approx(0.5f));
}

TEST_CASE("Path trail: the far end fades it out, the bands move with time, and the beacon stands at the end") {
    std::vector<glm::vec3> line{{0, 0, 0}, {500, 0, 0}};
    game::TrailStyle st;
    st.far_start = 50.0f;
    st.far_end = 60.0f;
    st.beacon = false;
    auto t = game::build_trail(line, st, 0.0);
    for (const auto& s : t) CHECK(s.position.x <= 60.0f + 1e-3f);

    // the bands: a band's peak moves towards the destination as time goes on (at 9 a second, 10 apart: the one at
    // 20 at time 0 is at 20.9 a tenth of a second later)
    auto brightest = [&](double time) {
        game::TrailStyle dense = st;
        dense.spacing = 0.1f;
        auto v = game::build_trail(line, dense, time);
        float x = 0.0f, best = -1.0f;
        for (const auto& s : v)
            if (s.position.x > 15.0f && s.position.x < 25.0f && s.size > best) { // (alpha is clamped at a band)
                best = s.size;
                x = s.position.x;
            }
        return x;
    };
    CHECK(brightest(0.0) == doctest::Approx(20.0f).epsilon(0.01));
    CHECK(brightest(0.1) == doctest::Approx(20.9f).epsilon(0.01));

    st.beacon = true;
    auto with = game::build_trail(line, st, 0.0);
    CHECK(with.size() == t.size() + 7);
    CHECK(with.back().position.x == doctest::Approx(500.0f));
    CHECK(with.back().position.z > 1.0f); // the column rises

    CHECK(game::build_trail(std::vector<glm::vec3>{{0, 0, 0}}, st, 0.0).empty());
    // no more than asked (and the beacon)
    st.far_end = 1000.0f;
    st.far_start = 900.0f;
    CHECK(game::build_trail(line, st, 0.0, {}, 20).size() == 27);
}

TEST_CASE("Guide path: found over the mesh, trimmed behind the player, found again off it, ends on arrival") {
    app::GuidePath g;
    int searches = 0;
    std::optional<std::vector<glm::vec3>> answer;
    auto path = [&](const glm::vec3& from, const glm::vec3& to) -> std::optional<std::vector<glm::vec3>> {
        ++searches;
        if (!answer) return std::nullopt;
        std::vector<glm::vec3> p{from, glm::vec3(50, 0, 0), to}; // round a corner at (50, 0)
        return p;
    };
    const glm::vec3 npc(50, 60, 0);
    g.start(7, "Banker Bob");
    REQUIRE(g.active());

    // the mesh not ready: said once, tried again after a second
    auto u = g.update({0, 0, 0}, npc, 0.0, path);
    CHECK(u.waiting_mesh);
    CHECK(g.line().empty());
    u = g.update({0, 0, 0}, npc, 0.5, path);
    CHECK_FALSE(u.waiting_mesh);
    CHECK(searches == 1);

    answer = std::vector<glm::vec3>{};
    u = g.update({0, 0, 0}, npc, 1.0, path);
    CHECK(searches == 2);
    REQUIRE(g.line().size() == 3);
    CHECK(g.line().front() == glm::vec3(0, 0, 0));
    CHECK(g.line().back() == npc);
    CHECK_FALSE(g.partial());

    // walking it: no new search, the line starts at the player, the corner passed is dropped
    u = g.update({30, 1, 0}, npc, 1.2, path);
    CHECK(searches == 2);
    CHECK(g.line().front() == glm::vec3(30, 1, 0));
    CHECK(g.line().size() == 3);
    u = g.update({50, 20, 0}, npc, 1.4, path);
    CHECK(searches == 2);
    CHECK(g.line().size() == 2);

    // off the line: found again (not sooner than kReplanGap after the last search)
    u = g.update({80, 20, 0}, npc, 1.45, path);
    CHECK(searches == 2);
    u = g.update({80, 20, 0}, npc, 2.0, path);
    CHECK(searches == 3);

    // the NPC moved: found again
    u = g.update({80, 20, 0}, glm::vec3(50, 80, 0), 3.0, path);
    CHECK(searches == 4);

    // arrived
    u = g.update({52, 75, 0}, glm::vec3(50, 80, 0), 3.2, path);
    CHECK(u.arrived);
    CHECK_FALSE(g.active());
    CHECK(g.line().empty());
}

TEST_CASE("Guide path: ends when the NPC is gone or the mesh has no way there") {
    app::GuidePath g;
    auto none = [](const glm::vec3&, const glm::vec3&) -> std::optional<std::vector<glm::vec3>> {
        return std::vector<glm::vec3>{};
    };
    g.start(7, "x");
    auto u = g.update({0, 0, 0}, glm::vec3(100, 0, 0), 0.0, none);
    CHECK(u.no_way);
    CHECK_FALSE(g.active());

    g.start(7, "x");
    u = g.update({0, 0, 0}, std::nullopt, 0.0, none);
    CHECK(u.gone);
    CHECK_FALSE(g.active());

    // a partial path: drawn as far as it goes
    auto part = [](const glm::vec3& from, const glm::vec3&) -> std::optional<std::vector<glm::vec3>> {
        return std::vector<glm::vec3>{from, glm::vec3(40, 0, 0)};
    };
    g.start(7, "x");
    u = g.update({0, 0, 0}, glm::vec3(100, 0, 0), 0.0, part);
    CHECK(g.active());
    CHECK(g.partial());
    CHECK(g.line().size() == 2);
}
