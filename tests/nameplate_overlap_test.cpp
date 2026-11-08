// Hailstone's overlap fade: projected positions do not enter the temporal state.

#include <doctest/doctest.h>

#include <cmath>

#include "game/nameplate_overlap.h"

using namespace nmsc::game;

namespace {
PlateBox box(float x, float y, float depth, bool front = false) { return {x, y, x + 100.0f, y + 20.0f, depth, front}; }
} // namespace

TEST_CASE("plate overlap: far names first, target last, equal depths stable") {
    const std::vector<PlateBox> b = {box(0, 0, 50), box(0, 0, 200), box(0, 0, 300, true), box(0, 0, 50)};
    CHECK(plate_draw_order(b) == std::vector<std::size_t>{1, 0, 3, 2});
}

TEST_CASE("plate overlap: empty and separate boxes remain visible") {
    CHECK(plate_draw_order({}).empty());
    CHECK(plate_target_alpha({}).empty());
    const std::vector<PlateBox> b = {box(0, 0, 100), box(300, 0, 20), box(0, 100, 5)};
    CHECK(plate_target_alpha(b) == std::vector<float>{1, 1, 1});
}

TEST_CASE("plate overlap: ten to twenty percent coverage fades to ten percent alpha") {
    std::vector<PlateBox> b = {box(0, 0, 100), box(95, 0, 20)};
    CHECK(plate_target_alpha(b)[0] == 1.0f);
    b[1].x0 = 90;
    CHECK(plate_target_alpha(b)[0] == doctest::Approx(1.0f));
    b[1].x0 = 85;
    CHECK(plate_target_alpha(b)[0] == doctest::Approx(0.55f));
    b[1].x0 = 80;
    CHECK(plate_target_alpha(b)[0] == doctest::Approx(0.1f));
    b[1] = box(0, 0, 20);
    const auto a = plate_target_alpha(b);
    CHECK(a[0] == doctest::Approx(0.1f));
    CHECK(a[1] == 1.0f);
}

TEST_CASE("plate overlap: target is never faded even behind the nearer name") {
    const std::vector<PlateBox> b = {box(0, 0, 10), box(0, 0, 300, true)};
    const auto a = plate_target_alpha(b);
    CHECK(a[0] == doctest::Approx(0.1f));
    CHECK(a[1] == 1.0f);
}

TEST_CASE("plate overlap: equal depths and degenerate boxes do not fade") {
    std::vector<PlateBox> b = {box(0, 0, 10), box(0, 0, 10)};
    CHECK(plate_target_alpha(b) == std::vector<float>{1, 1});
    b[0] = {0, 0, 0, 0, 100, false};
    CHECK(plate_target_alpha(b) == std::vector<float>{1, 1});
}

TEST_CASE("plate overlap: coverage is the largest single nearer box, input positions unchanged") {
    const std::vector<PlateBox> b = {box(0, 0, 100), box(-95, 0, 20), box(95, 0, 10)};
    CHECK(plate_target_alpha(b)[0] == 1.0f);
    CHECK(b[0].x0 == 0);
    CHECK(b[0].y0 == 0);
    CHECK(b[1].y0 == 0);
}

TEST_CASE("plate overlap: fading is smooth and independent of frame subdivision") {
    PlateFade a, b;
    CHECK(a.step(7, 0.1f, 0) == doctest::Approx(0.1f));
    b.step(7, 0.1f, 0);
    CHECK(a.step(7, 1.0f, 0) == doctest::Approx(0.1f));
    CHECK(a.step(7, 1.0f, -1.0f) == doctest::Approx(0.1f));
    const float once = a.step(7, 1.0f, 0.3f);
    CHECK(once > 0.1f);
    CHECK(once < 1.0f);
    b.step(7, 1.0f, 0.1f);
    b.step(7, 1.0f, 0.1f);
    CHECK(b.step(7, 1.0f, 0.1f) == doctest::Approx(once));
}

TEST_CASE("plate overlap: selecting a faded name restores full opacity immediately and stores it") {
    PlateFade f;
    CHECK(f.step(7, 0.1f, 0) == doctest::Approx(0.1f));
    CHECK(f.step(7, 0.1f, 0, true) == 1.0f);
    CHECK(f.step(7, 1.0f, 0) == 1.0f);
    CHECK(f.step(7, 0.1f, 0.016f) > 0.9f);
}

TEST_CASE("plate overlap: absent names and a cleared scene forget old opacity") {
    PlateFade f;
    f.step(7, 0.1f, 0);
    f.end_frame();
    f.end_frame();
    CHECK(f.step(7, 1.0f, 0) == 1.0f);
    f.clear();
    CHECK(f.step(7, 0.1f, 0) == doctest::Approx(0.1f));
}

TEST_CASE("plate overlap: no distance fade for isolated names") {
    const std::vector<PlateBox> b = {box(0, 0, 10), box(300, 0, 250)};
    CHECK(plate_target_alpha(b) == std::vector<float>{1, 1});
}

TEST_CASE("plate overlap: translating or scaling screen boxes preserves the coverage fade") {
    std::uint32_t rng = 7;
    auto number = [&] {
        rng = rng * 1664525u + 1013904223u;
        return static_cast<float>(rng >> 24);
    };
    for (int run = 0; run < 50; ++run) {
        std::vector<PlateBox> boxes;
        for (int i = 0; i < 20; ++i) {
            const float x = number(), y = number(), depth = number() + 1.0f;
            boxes.push_back(box(x, y, depth, i == 0));
        }
        auto transformed = boxes;
        for (auto& b : transformed) {
            b.x0 = b.x0 * 2.0f - 100.0f;
            b.x1 = b.x1 * 2.0f - 100.0f;
            b.y0 = b.y0 * 2.0f + 40.0f;
            b.y1 = b.y1 * 2.0f + 40.0f;
            b.depth *= 10.0f;
        }
        const auto alpha = plate_target_alpha(boxes), scaled = plate_target_alpha(transformed);
        CHECK(plate_draw_order(boxes) == plate_draw_order(transformed));
        for (std::size_t i = 0; i < alpha.size(); ++i) {
            CHECK(std::isfinite(alpha[i]));
            CHECK(alpha[i] >= kPlateCoveredAlpha - 1e-6f);
            CHECK(alpha[i] <= 1.0f);
            CHECK(alpha[i] == doctest::Approx(scaled[i]));
        }
    }
}
