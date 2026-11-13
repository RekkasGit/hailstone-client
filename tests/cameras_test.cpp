// The original's in-game cameras 1..5 (game/cameras.h): defaults, updates, ToggleView, the keys and the mouse.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <optional>

#include "game/cameras.h"

using namespace hailstone::game;

namespace {

// No scenery: every sweep reaches its point.
const CameraSweep kOpen = [](const glm::vec3&, const glm::vec3& to, float) -> std::optional<glm::vec3> { return to; };

CameraTarget target_at(glm::vec3 p, float heading = 0.0f, float eye = 5.0f) {
    CameraTarget t;
    t.position = p;
    t.heading = heading;
    t.eye_height = eye;
    return t;
}

// Two headings within a unit of each other around the circle (the bearing is the original's x87 one: 0 may come
// out as 511.5).
bool near_heading(float a, float b) {
    float d = std::fmod(std::fabs(a - b), 512.0f);
    return std::min(d, 512.0f - d) <= 1.0f;
}

} // namespace

TEST_CASE("CDisplay_ResetCameraDefaults: the overhead and user cameras' defaults") {
    std::array<GameCamera, 6> c;
    c[kCameraChase].f.distance = 50.0f;
    reset_camera_defaults(c);
    CHECK(c[kCameraOverhead].f.height == 25.0f);
    CHECK(c[kCameraOverhead].f.pitch == -100.0f);
    CHECK(c[kCameraChase].f.distance == 0.0f); // the English locale's chase camera is all zeros
    CHECK(c[kCameraUser1].f.distance == 30.0f);
    CHECK(c[kCameraUser1].f.heading == 192.0f);
    CHECK(c[kCameraUser1].f.height == 5.0f);
    CHECK(c[kCameraUser1].f.look_pitch);
    CHECK(c[kCameraUser2].f.distance == 82.0f);
    CHECK(c[kCameraUser2].f.heading == 277.0f);
    CHECK(c[kCameraUser2].f.height == 18.0f);
    CHECK(c[kCameraUser2].f.look_pitch);
    CHECK(c[kCameraTether].f.distance == 0.0f);
    for (const auto& cam : c) CHECK(cam.f.fov == 90.0f);
}

TEST_CASE("CDisplay::ToggleView cycles 0..5; the wheel-follow camera counts as 0") {
    CHECK(toggle_view_next(kCameraFirstPerson) == kCameraOverhead);
    CHECK(toggle_view_next(kCameraOverhead) == kCameraChase);
    CHECK(toggle_view_next(kCameraChase) == kCameraUser1);
    CHECK(toggle_view_next(kCameraUser1) == kCameraUser2);
    CHECK(toggle_view_next(kCameraUser2) == kCameraTether);
    CHECK(toggle_view_next(kCameraTether) == kCameraFirstPerson);
    CHECK(toggle_view_next(kCameraWheelFollow) == kCameraOverhead);
    CHECK(toggle_view_next(kCameraIsometric) == kCameraOverhead);
    // Virtual +0x2c: the chase and user cameras only.
    CHECK_FALSE(camera_active(kCameraFirstPerson));
    CHECK_FALSE(camera_active(kCameraOverhead));
    CHECK(camera_active(kCameraChase));
    CHECK(camera_active(kCameraUser1));
    CHECK(camera_active(kCameraUser2));
    CHECK_FALSE(camera_active(kCameraTether));
    CHECK_FALSE(camera_active(kCameraWheelFollow));
}

TEST_CASE("Overhead camera: above the eye by the height, looking the spawn's way") {
    std::array<GameCamera, 6> c;
    reset_camera_defaults(c);
    CameraPose p = update_camera(kCameraOverhead, c[kCameraOverhead], target_at({10, 20, 3}, 100.0f), kOpen);
    CHECK(p.position.x == 10.0f);
    CHECK(p.position.y == 20.0f);
    CHECK(p.position.z == 3.0f + 5.0f + 25.0f);
    CHECK(p.heading == 100.0f);
    CHECK(p.pitch == -100.0f);
    // GameState 5's clamp: the height 0..30.
    c[kCameraOverhead].f.height = 40.0f;
    p = update_camera(kCameraOverhead, c[kCameraOverhead], target_at({10, 20, 3}), kOpen);
    CHECK(p.position.z == 3.0f + 5.0f + 30.0f);
    // Ducking lowers only the pivot; riding without the seat bone raises both by 3.5.
    CameraTarget t = target_at({10, 20, 3});
    t.riding = true;
    p = update_camera(kCameraOverhead, c[kCameraOverhead], t, kOpen);
    CHECK(p.position.z == 3.0f + 5.0f + 3.5f + 30.0f);
    t.mount_seat_z = 9.0f;
    p = update_camera(kCameraOverhead, c[kCameraOverhead], t, kOpen);
    CHECK(p.position.z == 9.0f + 5.0f + 30.0f);
}

TEST_CASE("Chase camera: behind by eye height + distance, looking at the spawn, 8.5 below its pitch") {
    std::array<GameCamera, 6> c;
    reset_camera_defaults(c);
    GameCamera& cam = c[kCameraChase];
    cam.f.distance = 10.0f;
    cam.f.height = 2.0f;
    cam.f.pitch = 4.0f;
    CameraPose p = update_camera(kCameraChase, cam, target_at({0, 0, 0}, 0.0f), kOpen);
    CHECK(p.position.x == doctest::Approx(-15.0f));
    CHECK(p.position.y == doctest::Approx(0.0f));
    CHECK(p.position.z == 7.0f);
    CHECK(p.pitch == -4.5f);
    // The bearing from the camera to the spawn: the spawn's own heading here (heading 0 faces +x).
    CHECK(near_heading(p.heading, 0.0f));
    // Heading 128 faces +y: behind is -y.
    p = update_camera(kCameraChase, cam, target_at({0, 0, 0}, 128.0f), kOpen);
    CHECK(p.position.x == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(p.position.y == doctest::Approx(-15.0f));
    CHECK(near_heading(p.heading, 128.0f));
    // +0x45: the spawn's heading itself.
    cam.f.fixed_heading = true;
    p = update_camera(kCameraChase, cam, target_at({0, 0, 0}, 77.0f), kOpen);
    CHECK(p.heading == 77.0f);
    CHECK(cam.previous == glm::vec3(0.0f, -15.0f, 7.0f));
}

TEST_CASE("unk_EQCamera_SetPosition: a sweep stops the camera; a refused sweep leaves it") {
    std::array<GameCamera, 6> c;
    reset_camera_defaults(c);
    GameCamera& cam = c[kCameraChase];
    cam.f.distance = 10.0f;
    // A wall halfway: the camera stops there (the pivot sweep up to the eye is open).
    const CameraSweep wall = [](const glm::vec3& from, const glm::vec3& to, float radius) -> std::optional<glm::vec3> {
        CHECK(radius == 0.25f);
        if (from.x == to.x && from.y == to.y) return to;
        return from + (to - from) * 0.5f;
    };
    CameraPose p = update_camera(kCameraChase, cam, target_at({0, 0, 0}), wall);
    CHECK(p.position.x == doctest::Approx(-7.5f));
    // A refused sweep: the camera stays where it was.
    const CameraSweep refused = [](const glm::vec3&, const glm::vec3&, float) -> std::optional<glm::vec3> {
        return std::nullopt;
    };
    p = update_camera(kCameraChase, cam, target_at({50, 0, 0}), refused);
    CHECK(p.position.x == doctest::Approx(-7.5f));
}

TEST_CASE("User cameras: around the spawn at +0x38, pitched at its eye with +0x44") {
    std::array<GameCamera, 6> c;
    reset_camera_defaults(c);
    GameCamera& cam = c[kCameraUser1];
    cam.f.heading = 0.0f; // out along +x
    CameraPose p = update_camera(kCameraUser1, cam, target_at({0, 0, 0}), kOpen);
    CHECK(p.position.x == 30.0f);
    CHECK(p.position.z == 10.0f); // eye 5 + height 5
    CHECK(near_heading(p.heading, 256.0f)); // looking back at the spawn (-x)
    // 5 above the eye at 30 out: atan(5 / 30) = 13.45 512ths, below the horizon: 512 - 13.
    CHECK(p.pitch == 499.0f);
    // A pitch key (the user camera's pitch setter) clears +0x44: the pitch is +0x30 again.
    set_camera_pitch(kCameraUser1, cam, -20.0f);
    CHECK_FALSE(cam.f.look_pitch);
    p = update_camera(kCameraUser1, cam, target_at({0, 0, 0}), kOpen);
    CHECK(p.pitch == -20.0f);
    // CENTERVIEW sets it again.
    center_view(kCameraUser1, cam);
    CHECK(cam.f.look_pitch);
    // GameState 5's clamps: the distance 2..200.
    cam.f.distance = 1.0f;
    p = update_camera(kCameraUser1, cam, target_at({0, 0, 0}), kOpen);
    CHECK(p.position.x == 2.0f);
    cam.f.distance = 300.0f;
    update_camera(kCameraUser1, cam, target_at({0, 0, 0}), kOpen);
    CHECK(cam.f.distance == 200.0f);
    // The heading is FixHeading'd.
    cam.f.heading = 600.0f;
    update_camera(kCameraUser1, cam, target_at({0, 0, 0}), kOpen);
    CHECK(cam.f.heading == 88.0f);
}

TEST_CASE("Tether camera: kept direction, pulled along at 4..20, a one-frame turn") {
    std::array<GameCamera, 6> c;
    reset_camera_defaults(c);
    GameCamera& cam = c[kCameraTether];
    // The first frame (straight above the spawn): behind its heading at 4.
    CameraPose p = update_camera(kCameraTether, cam, target_at({0, 0, 0}, 0.0f), kOpen);
    CHECK(p.position.x == doctest::Approx(-4.0f));
    CHECK(p.position.y == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(p.position.z == doctest::Approx(5.0f));
    // The spawn moves 10 along +x: the camera, 14 off, stays.
    p = update_camera(kCameraTether, cam, target_at({10, 0, 0}), kOpen);
    CHECK(p.position.x == doctest::Approx(-4.0f));
    // 24 off: pulled to 20.
    p = update_camera(kCameraTether, cam, target_at({20, 0, 0}), kOpen);
    CHECK(p.position.x == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(near_heading(p.heading, 0.0f));
    // +0x38 turns it once (128 = a quarter turn), then is cleared.
    cam.f.heading = 128.0f;
    p = update_camera(kCameraTether, cam, target_at({20, 0, 0}), kOpen);
    CHECK(cam.f.heading == 0.0f);
    CHECK(p.position.x == doctest::Approx(20.0f).epsilon(1e-4));
    CHECK(p.position.y == doctest::Approx(-20.0f));
    // The pitch raises it: 64 (45 degrees) gives as much height as ground distance... tan(-pitch): below for +pitch.
    cam.f.pitch = 64.0f;
    p = update_camera(kCameraTether, cam, target_at({20, 0, 0}), kOpen);
    CHECK(p.position.z == doctest::Approx(5.0f - 20.0f / std::sqrt(2.0f)).epsilon(1e-4));
}

TEST_CASE("HandleMouseLook in the cameras 1..5") {
    std::array<GameCamera, 6> c;
    reset_camera_defaults(c);
    // The overhead: the player turns; up and down move the pitch (+-128).
    CHECK(camera_mouse_look(kCameraOverhead, c[kCameraOverhead], 5.0f, 10.0f, 3.0f, 4.0f, 1.0f, false));
    CHECK(c[kCameraOverhead].f.pitch == -110.0f);
    camera_mouse_look(kCameraOverhead, c[kCameraOverhead], 0.0f, 40.0f, 3.0f, 4.0f, 1.0f, false);
    CHECK(c[kCameraOverhead].f.pitch == -128.0f);
    // The chase camera: the height, by the raw motion clamped to sensitivity x 0.3, times the rate.
    c[kCameraChase].f.height = 10.0f;
    CHECK(camera_mouse_look(kCameraChase, c[kCameraChase], 0.0f, 0.0f, 3.0f, 4.0f, 0.5f, false));
    CHECK(c[kCameraChase].f.height == doctest::Approx(10.0f - 1.2f * 0.5f));
    // A user camera: the camera turns, not the player; up and down the distance (at least 3).
    CHECK_FALSE(camera_mouse_look(kCameraUser1, c[kCameraUser1], 10.0f, 0.0f, 1.0f, 4.0f, 1.0f, false));
    CHECK(c[kCameraUser1].f.heading == 202.0f);
    CHECK(c[kCameraUser1].f.distance == 31.0f);
    camera_mouse_look(kCameraUser1, c[kCameraUser1], 0.0f, 0.0f, -100.0f, 4.0f, 1.0f, false);
    CHECK(c[kCameraUser1].f.distance == 29.0f); // clamped to -2
    c[kCameraUser1].f.distance = 3.5f;
    camera_mouse_look(kCameraUser1, c[kCameraUser1], 0.0f, 0.0f, -2.0f, 4.0f, 1.0f, false);
    CHECK(c[kCameraUser1].f.distance == 3.0f);
    // Under water the user cameras take the pitch instead (and clear +0x44).
    camera_mouse_look(kCameraUser1, c[kCameraUser1], 0.0f, 10.0f, 1.0f, 4.0f, 1.0f, true);
    CHECK(c[kCameraUser1].f.pitch == -10.0f);
    CHECK_FALSE(c[kCameraUser1].f.look_pitch);
    CHECK(c[kCameraUser1].f.distance == 3.0f);
    // The tether: the camera turns; the pitch -120..64.
    CHECK_FALSE(camera_mouse_look(kCameraTether, c[kCameraTether], -3.0f, -100.0f, 0.0f, 4.0f, 1.0f, false));
    CHECK(c[kCameraTether].f.heading == 509.0f);
    CHECK(c[kCameraTether].f.pitch == 64.0f);
}

TEST_CASE("CENTERVIEW: the overhead's pitch back to -100, the others' look-at flag") {
    std::array<GameCamera, 6> c;
    reset_camera_defaults(c);
    c[kCameraOverhead].f.pitch = 20.0f;
    center_view(kCameraOverhead, c[kCameraOverhead]);
    CHECK(c[kCameraOverhead].f.pitch == -100.0f);
    CHECK_FALSE(c[kCameraOverhead].f.look_pitch);
    center_view(kCameraChase, c[kCameraChase]);
    CHECK(c[kCameraChase].f.look_pitch);
    center_view(kCameraTether, c[kCameraTether]);
    CHECK(c[kCameraTether].f.look_pitch);
}

TEST_CASE("wheel_follow_position: the camera behind the player stops where the sweep does, without steps") {
    const glm::vec3 eye(0.0f, 0.0f, 5.0f), back(-1.0f, 0.0f, 0.0f);
    CHECK(wheel_follow_position(eye, back, 15.0f, kOpen) == glm::vec3(-15.0f, 0.0f, 5.0f));
    CHECK(wheel_follow_position(eye, back, 15.0f, {}) == glm::vec3(-15.0f, 0.0f, 5.0f));
    // A wall at x = wall - 0.25 stops the sphere's centre; the radius asked for is the original's 0.25.
    float wall = -6.0f;
    float radius = 0.0f;
    const CameraSweep walled = [&](const glm::vec3& from, const glm::vec3& to, float r) -> std::optional<glm::vec3> {
        radius = r;
        const float stop = wall + r;
        return to.x < stop ? glm::vec3(stop, from.y, to.z) : to;
    };
    CHECK(wheel_follow_position(eye, back, 15.0f, walled).x == doctest::Approx(-5.75f));
    CHECK(radius == 0.25f);
    // Moving a little toward the wall moves the camera as little: no jump between steps.
    const glm::vec3 a = wheel_follow_position(eye, back, 15.0f, walled);
    const glm::vec3 b = wheel_follow_position(eye + glm::vec3(-0.1f, 0.0f, 0.0f), back, 15.0f, walled);
    CHECK(glm::distance(a, b) <= 0.1f + 1e-4f);
    // A refused sweep keeps the camera at the eye (never through the scenery).
    const CameraSweep refused = [](const glm::vec3&, const glm::vec3&, float) -> std::optional<glm::vec3> {
        return std::nullopt;
    };
    CHECK(wheel_follow_position(eye, back, 15.0f, refused) == eye);
}

TEST_CASE("knockout's overhead camera keeps the wheel camera's distance and restores it") {
    CameraSelection s{kCameraWheelFollow, 35.0f, 20.0f};
    const int saved = s.type;
    select_camera(s, kCameraOverhead);
    CHECK(s.type == kCameraOverhead);
    CHECK(s.distance == 0.0f); // nothing else reads camera 6's distance while overhead is active
    CHECK(s.pad_distance == 35.0f);
    restore_camera(s, saved, 150.0f);
    CHECK(s.type == kCameraWheelFollow);
    CHECK(s.distance == 35.0f);

    CameraSelection first{kCameraFirstPerson, 0.0f, 20.0f};
    select_camera(first, kCameraOverhead);
    restore_camera(first, kCameraFirstPerson, 150.0f);
    CHECK(first.type == kCameraFirstPerson);
    CHECK(first.distance == 0.0f);
    CHECK(first.pad_distance == 20.0f);
}
