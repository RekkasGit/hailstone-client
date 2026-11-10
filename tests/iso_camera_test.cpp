// Hailstone's isometric camera (game/iso_camera.h).
#include <doctest/doctest.h>

#include <cmath>

#include <glm/gtc/constants.hpp>

#include "game/iso_camera.h"

using namespace hailstone::game;

TEST_CASE("iso camera: looks down at the focus from its distance") {
    IsoCameraSettings s;
    IsoCamera cam;
    cam.yaw = glm::half_pi<float>(); // looking along +Y
    cam.distance = 100.0f;
    IsoPose p = update_iso_camera(cam, {10.0f, 20.0f, 5.0f}, 0.016f, s);
    CHECK(cam.placed);
    CHECK(p.pitch == doctest::Approx(-s.pitch));
    CHECK(p.focus_distance == doctest::Approx(100.0f));
    CHECK(glm::distance(p.position, glm::vec3(10.0f, 20.0f, 5.0f)) == doctest::Approx(100.0f));
    // Behind the focus (less y), above it, and level with it in x.
    CHECK(p.position.x == doctest::Approx(10.0f).epsilon(1e-4));
    CHECK(p.position.y < 20.0f);
    CHECK(p.position.z == doctest::Approx(5.0f + 100.0f * std::sin(s.pitch)));
    // The forward direction points from the camera to the focus.
    glm::vec3 to = glm::normalize(glm::vec3(10.0f, 20.0f, 5.0f) - p.position);
    glm::vec3 f = iso_forward(p.yaw, -p.pitch);
    CHECK(glm::dot(to, f) == doctest::Approx(1.0f));
}

TEST_CASE("iso camera: eases toward the player, snaps across a teleport") {
    IsoCameraSettings s;
    IsoCamera cam;
    update_iso_camera(cam, {0.0f, 0.0f, 0.0f}, 0.016f, s);
    update_iso_camera(cam, {10.0f, 0.0f, 0.0f}, 0.016f, s);
    CHECK(cam.focus.x > 0.0f);
    CHECK(cam.focus.x < 10.0f);
    for (int i = 0; i < 200; ++i) update_iso_camera(cam, {10.0f, 0.0f, 0.0f}, 0.016f, s);
    CHECK(cam.focus.x == doctest::Approx(10.0f).epsilon(1e-3));
    update_iso_camera(cam, {500.0f, 0.0f, 0.0f}, 0.016f, s);
    CHECK(cam.focus.x == doctest::Approx(500.0f));
}

TEST_CASE("iso camera: zoom stays in range, turn wraps") {
    IsoCameraSettings s;
    IsoCamera cam;
    cam.distance = 160.0f;
    iso_zoom(cam, 1.0f, s);
    CHECK(cam.distance == doctest::Approx(144.0f));
    iso_zoom(cam, -1.0f, s);
    CHECK(cam.distance > 144.0f);
    iso_zoom(cam, 100.0f, s);
    CHECK(cam.distance == doctest::Approx(s.min_distance));
    iso_zoom(cam, -100.0f, s);
    CHECK(cam.distance == doctest::Approx(s.max_distance));
    cam.yaw = 3.0f;
    iso_turn(cam, 1.0f);
    CHECK(cam.yaw == doctest::Approx(4.0f - glm::two_pi<float>()));
}

TEST_CASE("iso camera: the view is measured from the player") {
    ViewDistance v;
    v.fog_start = 100.0f;
    v.fog_end = 300.0f;
    v.far_plane = 400.0f;
    ViewDistance w = iso_view(v, 160.0f);
    CHECK(w.fog_start == doctest::Approx(260.0f));
    CHECK(w.fog_end == doctest::Approx(460.0f));
    CHECK(w.far_plane == doctest::Approx(560.0f));
    CHECK(iso_view(v, -5.0f).far_plane == doctest::Approx(400.0f));
    CHECK(iso_shadow_near(160.0f, 1.0f) == doctest::Approx(160.0f - kIsoShadowLead));
    CHECK(iso_shadow_near(10.0f, 1.0f) == doctest::Approx(1.0f));
    CHECK(iso_fov_units(90.0f) == doctest::Approx(128.0f));
}
