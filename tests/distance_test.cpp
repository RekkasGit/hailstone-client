#include <doctest/doctest.h>

#include "game/distance.h"

using nmsc::game::player_distance;
using nmsc::game::true_distance;

TEST_CASE("TrueDistance: 3D with z_scale 1, 2D with z_scale 0, scaled height otherwise") {
    glm::vec3 a(0, 0, 0), b(3, 4, 12);
    CHECK(true_distance(a, b, 1.0f) == doctest::Approx(13.0f));
    CHECK(true_distance(a, b, 0.0f) == doctest::Approx(5.0f));
    CHECK(true_distance(a, b, 2.0f) == doctest::Approx(std::sqrt(25.0f + 36.0f)));
}

TEST_CASE("PlayerDistance: -1e27 when a spawn is missing") {
    glm::vec3 a(1, 2, 3);
    CHECK(player_distance(&a, nullptr) == -1e27f);
    CHECK(player_distance(nullptr, &a) == -1e27f);
    CHECK(player_distance(&a, &a) == 0.0f);
}
