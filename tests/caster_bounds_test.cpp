// render/caster_bounds.h: the placed objects' bounding spheres, as the shadows pick their casters.
#include <doctest/doctest.h>

#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "render/caster_bounds.h"

using namespace hailstone::render;

TEST_CASE("caster bounds: the sphere round a model's points is centred on their box and holds them all") {
    // A door slab 2 x 12 x 20, hinged at the origin.
    std::vector<glm::vec3> points = {{0, 0, 0}, {2, 0, 0}, {0, 12, 0}, {2, 12, 20}, {0, 0, 20}};
    BoundingSphere s = bounding_sphere(points);
    CHECK(s.center.x == doctest::Approx(1.0f));
    CHECK(s.center.y == doctest::Approx(6.0f));
    CHECK(s.center.z == doctest::Approx(10.0f));
    for (const glm::vec3& p : points) CHECK(glm::length(p - s.center) <= s.radius + 1e-4f);
    CHECK(s.radius == doctest::Approx(glm::length(glm::vec3(1, 6, 10))));
    CHECK(bounding_sphere({}).radius == 0.0f);
}

TEST_CASE("caster bounds: a placed sphere moves with the matrix and grows with its largest scale") {
    BoundingSphere local{{1, 6, 10}, 5.0f};
    // Turned a quarter about z (a door swinging open), then moved and scaled.
    glm::mat4 t = glm::translate(glm::mat4(1.0f), glm::vec3(100, 50, -3));
    t = glm::rotate(t, glm::radians(90.0f), glm::vec3(0, 0, 1));
    t = glm::scale(t, glm::vec3(1.0f, 2.0f, 1.5f));
    BoundingSphere w = transform_sphere(t, local);
    glm::vec3 expect = glm::vec3(t * glm::vec4(local.center, 1.0f));
    CHECK(w.center.x == doctest::Approx(expect.x));
    CHECK(w.center.y == doctest::Approx(expect.y));
    CHECK(w.center.z == doctest::Approx(expect.z));
    CHECK(w.radius == doctest::Approx(10.0f));
}

TEST_CASE("caster bounds: a caster is in a light's reach while the two spheres overlap") {
    BoundingSphere s{{0, 0, 0}, 10.0f};
    CHECK(sphere_in_reach(s, {50, 0, 0}, 45.0f));
    CHECK_FALSE(sphere_in_reach(s, {50, 0, 0}, 40.0f));
    CHECK_FALSE(sphere_in_reach(s, {0, 0, 200}, 100.0f));
}
