#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

#include "render/light_grid.h"

using namespace hailstone;

namespace {

std::vector<std::uint32_t> lights_in(const render::LightGrid& g, glm::vec2 p) {
    auto x = static_cast<std::uint32_t>((p.x - g.origin.x) / g.cell);
    auto y = static_cast<std::uint32_t>((p.y - g.origin.y) / g.cell);
    glm::uvec2 c = g.cells.at(std::size_t(y) * g.width + x);
    return {g.indices.begin() + c.x, g.indices.begin() + c.x + c.y};
}

} // namespace

TEST_CASE("light grid: each cell lists the lights that reach it") {
    std::vector<assets::PointLight> lights = {
        {{0.0f, 0.0f, 0.0f}, {1, 1, 1}, 50.0f},
        {{300.0f, 0.0f, 10.0f}, {1, 0, 0}, 20.0f},
        {{150.0f, 150.0f, 0.0f}, {1, 1, 1}, 0.0f}, // no radius: left out
    };
    render::LightGrid g = render::build_light_grid(lights, 64.0f);
    CHECK(g.origin == glm::vec2(-50.0f, -50.0f));
    CHECK(g.width == 6); // -50 .. 320 in 64-unit cells
    CHECK(g.height == 2);
    CHECK(lights_in(g, {0.0f, 0.0f}) == std::vector<std::uint32_t>{0});
    CHECK(lights_in(g, {300.0f, 0.0f}) == std::vector<std::uint32_t>{1});
    CHECK(lights_in(g, {150.0f, 0.0f}).empty());
    CHECK(std::find(g.indices.begin(), g.indices.end(), 2u) == g.indices.end());
}

TEST_CASE("light grid: crowded cells keep the nearest lights, and a wide spread grows the cells") {
    std::vector<assets::PointLight> lights;
    for (int i = 0; i < 30; ++i) lights.push_back({{float(i), 0.0f, 0.0f}, {1, 1, 1}, 10.0f});
    render::LightGrid g = render::build_light_grid(lights, 64.0f, 4);
    for (const auto& c : g.cells) CHECK(c.y <= 4);

    std::vector<assets::PointLight> far = {{{0, 0, 0}, {1, 1, 1}, 10.0f}, {{100000, 100000, 0}, {1, 1, 1}, 10.0f}};
    render::LightGrid wide = render::build_light_grid(far, 64.0f);
    CHECK(std::size_t(wide.width) * wide.height <= render::kMaxGridCells);
    CHECK(lights_in(wide, {100000.0f, 100000.0f}) == std::vector<std::uint32_t>{1});
}

TEST_CASE("light grid: no lights, no grid") {
    render::LightGrid g = render::build_light_grid({}, 64.0f);
    CHECK(g.width == 0);
    CHECK(g.cells.empty());
}

TEST_CASE("light grid: a light with a NaN or infinite place or reach is left out, the grid stays in bounds") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    std::vector<assets::PointLight> lights = {
        {{nan, 0.0f, 0.0f}, {1, 1, 1}, 50.0f},
        {{0.0f, 0.0f, 0.0f}, {1, 1, 1}, inf},
        {{0.0f, inf, 0.0f}, {1, 1, 1}, 10.0f},
        {{10.0f, 10.0f, 0.0f}, {1, 1, 1}, nan},
        {{3.0e38f, 0.0f, 0.0f}, {1, 1, 1}, 1.0e38f}, // its reach past the float range
        {{0.0f, 0.0f, 0.0f}, {1, 1, 1}, 20.0f},     // the one usable
    };
    render::LightGrid g = render::build_light_grid(lights, 64.0f);
    REQUIRE(g.width > 0);
    CHECK(std::size_t(g.width) * g.height <= render::kMaxGridCells);
    CHECK(g.cells.size() == std::size_t(g.width) * g.height);
    CHECK(lights_in(g, {0.0f, 0.0f}) == std::vector<std::uint32_t>{5});
    for (std::uint32_t i : g.indices) CHECK(i == 5);
    // Only unusable lights: no grid; a bad cell size: none either.
    CHECK(render::build_light_grid(std::span(lights).first(5), 64.0f).cells.empty());
    CHECK(render::build_light_grid(lights, nan).cells.empty());
    CHECK(render::build_light_grid(lights, 0.0f).cells.empty());
}

TEST_CASE("light grid: lights spread across nearly the float range get coarse cells, within the cell count") {
    std::vector<assets::PointLight> lights = {{{-1.0e38f, -1.0e38f, 0.0f}, {1, 1, 1}, 10.0f},
                                              {{1.0e38f, 1.0e38f, 0.0f}, {1, 1, 1}, 10.0f}};
    render::LightGrid g = render::build_light_grid(lights, 64.0f);
    REQUIRE(g.width > 0);
    CHECK(std::isfinite(g.cell));
    CHECK(std::size_t(g.width) * g.height <= render::kMaxGridCells);
    CHECK(g.cells.size() == std::size_t(g.width) * g.height);
}
