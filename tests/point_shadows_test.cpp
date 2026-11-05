// render/point_shadows.h: the cube faces and the choice of lights.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "assets/model.h"
#include "render/point_shadows.h"
#include "render/scene_lighting.h"

using namespace nmsc;
using namespace nmsc::render;

namespace {

assets::PointLight light_at(glm::vec3 p, float radius, glm::vec3 color = glm::vec3(1.0f, 0.7f, 0.4f)) {
    assets::PointLight l;
    l.position = p;
    l.radius = radius;
    l.color = color;
    return l;
}

} // namespace

TEST_CASE("point shadow faces: the major axis picks the face, the other two over it give the point") {
    struct Case { glm::vec3 d; std::uint32_t face; glm::vec2 ndc; };
    const Case cases[] = {
        {{10, 2, -5}, 0, {0.2f, -0.5f}}, {{-10, 2, -5}, 1, {0.2f, -0.5f}}, {{3, 8, 4}, 2, {0.375f, 0.5f}},
        {{3, -8, 4}, 3, {0.375f, 0.5f}}, {{1, -2, 4}, 4, {0.25f, -0.5f}}, {{1, -2, -4}, 5, {0.25f, -0.5f}},
    };
    for (const Case& c : cases) {
        PointShadowTexel t = point_shadow_face(c.d);
        CHECK(t.face == c.face);
        CHECK(t.ndc.x == doctest::Approx(c.ndc.x * kPointShadowFaceScale));
        CHECK(t.ndc.y == doctest::Approx(c.ndc.y * kPointShadowFaceScale));
    }
    // On the face's edge the point stays inside it, by the margin the filter needs.
    PointShadowTexel edge = point_shadow_face({5, 5, -5});
    CHECK(std::abs(edge.ndc.x) < 1.0f);
    CHECK(std::abs(edge.ndc.y) < 1.0f);
}

TEST_CASE("point shadow faces: a box shows on the faces whose side of the light it reaches") {
    glm::vec3 light(0, 0, 0);
    // A box east of the light, above and below it.
    CHECK(box_reaches_face(light, 0, {5, -1, -1}, {6, 1, 1}));
    CHECK_FALSE(box_reaches_face(light, 1, {5, -1, -1}, {6, 1, 1}));
    CHECK(box_reaches_face(light, 4, {5, -1, -1}, {6, 1, 1}));
    CHECK(box_reaches_face(light, 5, {5, -1, -1}, {6, 1, 1}));
    // Wholly above it.
    CHECK_FALSE(box_reaches_face(light, 5, {-1, -1, 2}, {1, 1, 3}));
}

TEST_CASE("point shadows: the brightest lights near the camera get the slots, nearest built first") {
    std::vector<assets::PointLight> lights = {
        light_at({0, 0, 10}, 60),         // at the camera
        light_at({100, 0, 10}, 60),       // near
        light_at({300, 0, 10}, 60),       // further
        light_at({2000, 0, 10}, 60),      // out of range
        light_at({20, 0, 10}, 3),         // too small to shadow
        light_at({50, 0, 10}, 60, glm::vec3(0.05f)), // dim
    };
    PointShadows shadows(2);
    auto builds = shadows.update(lights, glm::vec3(0.0f), 0.016f);
    std::vector<int> chosen;
    for (const auto& s : shadows.slots()) chosen.push_back(s.light);
    CHECK(std::find(chosen.begin(), chosen.end(), 0) != chosen.end());
    CHECK(std::find(chosen.begin(), chosen.end(), 1) != chosen.end());
    // One cube a frame (kPointShadowBuildsPerFrame), the nearest light's first, the other's the next frame.
    REQUIRE(builds.size() == 1);
    CHECK(shadows.slots()[builds[0]].light == 0);
    shadows.mark_built(builds[0]);
    builds = shadows.update(lights, glm::vec3(0.0f), 0.016f);
    REQUIRE(builds.size() == 1);
    CHECK(shadows.slots()[builds[0]].light == 1);
    shadows.mark_built(builds[0]);
    // Built, they fade in; nothing more to build while the camera stays.
    CHECK(shadows.update(lights, glm::vec3(0.0f), 0.1f).empty());
    CHECK(shadows.slots()[0].strength == doctest::Approx(0.4f + 0.064f).epsilon(1e-3));
}

TEST_CASE("point shadows: a light keeps its slot against a slightly better one; a new zone empties them") {
    std::vector<assets::PointLight> lights = {light_at({0, 0, 0}, 60), light_at({120, 0, 0}, 60)};
    PointShadows shadows(1);
    shadows.update(lights, glm::vec3(55, 0, 0), 0.016f);
    REQUIRE(shadows.slots()[0].light == 0);
    shadows.mark_built(0);
    // The camera moves a little towards the other light: not enough to trade.
    shadows.update(lights, glm::vec3(62, 0, 0), 0.016f);
    CHECK(shadows.slots()[0].light == 0);
    CHECK(shadows.slots()[0].built);
    // Well past it, the other light takes the slot, to be built again.
    auto builds = shadows.update(lights, glm::vec3(160, 0, 0), 0.016f);
    CHECK(shadows.slots()[0].light == 1);
    CHECK_FALSE(shadows.slots()[0].built);
    CHECK(builds.size() == 1);
    shadows.reset();
    CHECK(shadows.slots()[0].light == -1);
}

TEST_CASE("point shadows: the scene lighting gets the built slots, their cubes, and fades with the distance") {
    std::vector<assets::PointLight> lights = {light_at({0, 0, 0}, 60), light_at({40, 0, 0}, 60)};
    PointShadows shadows(2);
    auto builds = shadows.update(lights, glm::vec3(0.0f), 0.016f);
    shadows.mark_built(builds[0]); // one built, one waiting
    shadows.update(lights, glm::vec3(0.0f), 1.0f);
    shadows.set_live(builds[0], true);
    SceneLighting lighting;
    shadows.apply(lighting, lights, glm::vec3(0.0f), 512);
    CHECK(lighting.point_shadow_params.x == 1.0f);
    CHECK(lighting.point_shadow_params.y == doctest::Approx(1.0f / 512.0f));
    std::uint32_t slot = builds[0];
    CHECK(glm::vec3(lighting.point_shadow_light[0]) ==
          lights[static_cast<std::size_t>(shadows.slots()[slot].light)].position);
    // Live: the slot's second cube, after the caches.
    CHECK(lighting.point_shadow_slot[0].x == static_cast<float>((2 + slot) * kPointShadowFaces));
    CHECK(lighting.point_shadow_slot[0].y == doctest::Approx(1.0f));
    // Far off, the shadow is faded out.
    shadows.apply(lighting, lights, glm::vec3(kPointShadowRange + 100.0f, 0, 0), 512);
    CHECK(lighting.point_shadow_params.x == 0.0f);
}

namespace {
game::LightSource source_at(glm::vec3 p, float radius, glm::vec3 color = glm::vec3(1.0f)) {
    game::LightSource l;
    l.position = p;
    l.radius = radius;
    l.color = color;
    return l;
}
} // namespace

TEST_CASE("point shadows: the moving lights' share of the slots") {
    CHECK(dynamic_shadow_slots(0).carried == 0);
    CHECK(dynamic_shadow_slots(2).carried == 0);
    CHECK(dynamic_shadow_slots(2).spells == 0);
    CHECK(dynamic_shadow_slots(3).carried == 1);
    CHECK(dynamic_shadow_slots(4).spells == 0);
    CHECK(dynamic_shadow_slots(5).spells == 1);
    CHECK(dynamic_shadow_slots(8).carried == 1);
    CHECK(dynamic_shadow_slots(8).spells == 2);
    PointShadows shadows(8);
    CHECK(shadows.total() == 8);
    CHECK(shadows.slot_count() == 5);
    CHECK(shadows.dynamic_slots().size() == 3);
    // The moving lights draw into the live cubes after the zone lights'.
    CHECK(shadows.dynamic_cube(0) == 8 + 5);
    CHECK(shadows.dynamic_cube(2) == 8 + 7);
}

TEST_CASE("point shadows: the carried light and the brightest spells get the moving slots") {
    PointShadows shadows(8);
    DynamicShadowLights moving;
    moving.carried = source_at({1.5f, 2.25f, 4.0f}, 65.0f);
    moving.carrier = 77;
    moving.spells = {
        source_at({30, 0, 0}, 60, glm::vec3(0.2f)), // dim
        source_at({20, 0, 0}, 80),                   // bright
        source_at({2000, 0, 0}, 100),                // out of range
        source_at({10, 0, 0}, 4),                    // too small to shadow
        source_at({-40, 0, 0}, 70),                  // bright too
    };
    shadows.set_dynamic(moving, glm::vec3(0.0f));
    auto d = shadows.dynamic_slots();
    REQUIRE(d[0].used);
    CHECK(d[0].position == moving.carried->position);
    CHECK(d[0].skip_spawn == 77);
    CHECK_FALSE(d[0].skip_around);
    REQUIRE(d[1].used);
    REQUIRE(d[2].used);
    CHECK(d[1].position == glm::vec3(20, 0, 0));
    CHECK(d[2].position == glm::vec3(-40, 0, 0));
    CHECK(d[1].skip_around);
    CHECK(d[1].skip_spawn == 0);

    // The scene lighting holds them at the exact positions the light list has, in their live cubes, with no zone
    // light built.
    SceneLighting lighting;
    std::vector<assets::PointLight> none;
    shadows.apply(lighting, none, glm::vec3(0.0f), 512);
    REQUIRE(lighting.point_shadow_params.x == 3.0f);
    CHECK(glm::vec3(lighting.point_shadow_light[0]) == moving.carried->position);
    CHECK(lighting.point_shadow_light[0].w == 65.0f);
    CHECK(lighting.point_shadow_slot[0].x == static_cast<float>(shadows.dynamic_cube(0) * kPointShadowFaces));
    CHECK(lighting.point_shadow_slot[0].y == doctest::Approx(1.0f));

    // Next frame without a torch and with one spell: the slots follow.
    moving.carried.reset();
    moving.spells.resize(1);
    shadows.set_dynamic(moving, glm::vec3(0.0f));
    CHECK_FALSE(shadows.dynamic_slots()[0].used);
    CHECK(shadows.dynamic_slots()[1].used);
    CHECK_FALSE(shadows.dynamic_slots()[2].used);
    shadows.apply(lighting, none, glm::vec3(0.0f), 512);
    CHECK(lighting.point_shadow_params.x == 1.0f);
    shadows.reset();
    CHECK_FALSE(shadows.dynamic_slots()[1].used);
}

TEST_CASE("point shadows: few slots all go to the zone's lights") {
    PointShadows shadows(2);
    DynamicShadowLights moving;
    moving.carried = source_at({0, 0, 0}, 65.0f);
    shadows.set_dynamic(moving, glm::vec3(0.0f));
    CHECK(shadows.dynamic_slots().empty());
    SceneLighting lighting;
    std::vector<assets::PointLight> none;
    shadows.apply(lighting, none, glm::vec3(0.0f), 512);
    CHECK(lighting.point_shadow_params.x == 0.0f);
}
