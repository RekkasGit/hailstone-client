#include "game/cameras.h"

#include <algorithm>
#include <cmath>

#include "game/doors.h" // fix_heading
#include "game/follow.h" // x87_bearing

namespace hailstone::game {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kSweepRadius = 0.25f; // 0x9c782c
constexpr float kDuckEye = 1.5f;      // 0x9c5a14
constexpr float kMountEye = 3.5f;     // 0x9c8334
constexpr float kChasePitch = 8.5f;   // 0xa002e0

// The eye z the wanted point is built on (zT) and the pivot's (zP): z + eye, the pivot at z + 1.5 when ducking;
// riding, both the mount's dag 0xe z + eye, or z + eye + 3.5 without it.
struct EyeZ {
    float wanted = 0.0f;
    float pivot = 0.0f;
};
EyeZ eye_z(const CameraTarget& t) {
    EyeZ z;
    z.wanted = t.position.z + t.eye_height;
    z.pivot = t.ducking ? t.position.z + kDuckEye : z.wanted;
    if (t.riding) {
        z.wanted = t.mount_seat_z ? t.eye_height + *t.mount_seat_z : t.position.z + t.eye_height + kMountEye;
        z.pivot = z.wanted;
    }
    return z;
}

// unk_EQCamera_SetPosition twice: the pivot from the spawn up to its eye, then the camera from the pivot to the wanted
// point. A refused sweep leaves its output as it was: the pivot then stays at the eye (the original's is stack it
// never set), the camera where it was.
void place(GameCamera& cam, const CameraTarget& t, float pivot_z, const glm::vec3& wanted, const CameraSweep& sweep) {
    glm::vec3 pivot(t.position.x, t.position.y, pivot_z);
    if (sweep)
        if (auto p = sweep(t.position, pivot, kSweepRadius)) pivot = *p;
    std::optional<glm::vec3> at = sweep ? sweep(pivot, wanted, kSweepRadius) : std::optional<glm::vec3>(wanted);
    if (at) cam.position = *at;
    else if (!cam.placed) cam.position = wanted;
    cam.placed = true;
}

// GameState 5's clamps: the overhead and chase cameras' distance at most 200, the user and tether cameras' 2..200;
// the height 0..30 (below 0 or NaN: 0).
void clamp_in_game(UserCamera& f, bool distance_floor) {
    if (distance_floor && !(f.distance >= 2.0f)) f.distance = 2.0f;
    else if (200.0f < f.distance) f.distance = 200.0f;
    if (!(0.0f <= f.height)) f.height = 0.0f;
    else if (30.0f < f.height) f.height = 30.0f;
}

float bearing_to(const glm::vec3& from, const glm::vec3& to) {
    return static_cast<float>(x87_bearing(from.x, from.y, to.x, to.y, mechanics::Fpu{}));
}

// CMath vtable +0x18 (EQGraphicsDX9 0x100b9490): the integer angle of (dz, r) in 512ths, 0..128 above, 512 - that
// below; straight up or down 128 / 384. Its tangent table's rounding is not traced (docs/unknowns.md): truncated
// here.
float table_atan2(float dz, float r) {
    if (!(r > 0.0f)) return dz >= 0.0f ? 128.0f : 384.0f;
    const float i = std::trunc(std::atan(std::fabs(dz) / r) * 256.0f / kPi);
    return dz >= 0.0f ? i : 512.0f - i;
}

CameraPose overhead(GameCamera& cam, const CameraTarget& t, const CameraSweep& sweep) {
    // EQCamera_Overhead::vf2 @0x7982d0: above the eye by the height, out along +0x38 by the distance (not
    // FixHeading'd); it looks the spawn's way.
    UserCamera& f = cam.f;
    clamp_in_game(f, false);
    const EyeZ z = eye_z(t);
    const glm::vec3 wanted(t.position.x + table_cos(f.heading) * f.distance,
                           t.position.y + table_sin(f.heading) * f.distance, z.wanted + f.height);
    place(cam, t, z.pivot, wanted, sweep);
    return {cam.position, t.heading, f.pitch};
}

CameraPose chase(GameCamera& cam, const CameraTarget& t, const CameraSweep& sweep) {
    // EQCamera_Chase::vf2 @0x796920: behind the spawn's heading by its eye height plus the distance, above its eye by
    // the height; it looks along the bearing to the spawn (or the spawn's heading with +0x45), 8.5 below +0x30
    // (GameState 1 has none).
    UserCamera& f = cam.f;
    clamp_in_game(f, false);
    if (f.height < 0.0f) f.height = 0.0f;
    const EyeZ z = eye_z(t);
    f.heading = fix_heading(f.heading);
    cam.previous = cam.position;
    const float back = t.eye_height + f.distance;
    const glm::vec3 wanted(t.position.x - table_cos(t.heading) * back, t.position.y - table_sin(t.heading) * back,
                           z.wanted + f.height);
    place(cam, t, z.pivot, wanted, sweep);
    const float heading = f.fixed_heading ? t.heading : bearing_to(cam.position, t.position);
    return {cam.position, heading, f.pitch - kChasePitch};
}

CameraPose user(GameCamera& cam, const CameraTarget& t, const CameraSweep& sweep) {
    // EQCamera_User::vf2 @0x798c90 in game: around the spawn at +0x38 and the distance, above its eye by the height;
    // the bearing to the spawn (or its heading with +0x45), and with +0x44 the pitch toward its eye.
    UserCamera& f = cam.f;
    cam.previous = cam.position;
    clamp_in_game(f, true);
    f.heading = fix_heading(f.heading);
    const EyeZ z = eye_z(t);
    const glm::vec3 wanted(t.position.x + table_cos(f.heading) * f.distance,
                           t.position.y + table_sin(f.heading) * f.distance, z.wanted + f.height);
    place(cam, t, z.pivot, wanted, sweep);
    CameraPose pose{cam.position, f.fixed_heading ? t.heading : bearing_to(cam.position, t.position), f.pitch};
    if (f.look_pitch) {
        const float dx = t.position.x - cam.position.x, dy = t.position.y - cam.position.y;
        pose.pitch = table_atan2(z.wanted - cam.position.z, std::sqrt(dx * dx + dy * dy));
    }
    return pose;
}

CameraPose tether(GameCamera& cam, const CameraTarget& t, const CameraSweep& sweep) {
    // The tether camera's vf2 @0x7986b0: it keeps last frame's direction from the spawn (or, straight above it,
    // behind its heading), turned by +0x38 (then cleared: a one-frame turn) and raised by the pitch, and is pulled
    // along at 4..20 units; it looks along the bearing to the spawn.
    UserCamera& f = cam.f;
    EyeZ z = eye_z(t);
    if (!cam.placed) cam.position = glm::vec3(t.position.x, t.position.y, z.wanted); // straight above: behind it
    cam.previous = cam.position;
    clamp_in_game(f, true); // never read
    // Sitting (or the actor's animation 0x21 / 0x24, +0x10c0, not modelled: docs/unknowns.md): bone 0's z + a tenth
    // of the eye height.
    if (t.sitting && t.root_bone_z) z.wanted = *t.root_bone_z + 0.1f * t.eye_height;
    glm::vec3 u = cam.previous - glm::vec3(t.position.x, t.position.y, z.wanted);
    if (std::fabs(u.x) < 0.01f && std::fabs(u.y) < 0.01f) {
        // CMath +0x04 / +0x00, the interpolated table (its interpolation not traced: the exact cosine here).
        const float a = t.heading * kPi / 256.0f;
        u = glm::vec3(-std::cos(a), -std::sin(a), 0.0f);
    }
    const float length = glm::length(u);
    u = length > 1.19e-7f ? u / length : glm::vec3(0.0f);
    const float r = std::sqrt(u.x * u.x + u.y * u.y);
    const float theta = std::atan2(u.y, u.x) + f.heading * (1.0f / 256.0f) * kPi;
    glm::vec3 v(r * std::cos(theta), r * std::sin(theta), r * std::tan(-f.pitch * kPi / 256.0f));
    const float vl = glm::length(v);
    v = vl > 1.19e-7f ? v / vl : glm::vec3(0.0f);
    f.heading = 0.0f;
    const float d = std::clamp(length, 4.0f, 20.0f);
    const glm::vec3 wanted(t.position.x + v.x * d, t.position.y + v.y * d, z.wanted + v.z * d);
    place(cam, t, z.pivot, wanted, sweep);
    return {cam.position, bearing_to(cam.position, t.position), f.pitch};
}

} // namespace

void reset_camera_defaults(std::array<GameCamera, 6>& cameras) {
    for (std::size_t i = 1; i < cameras.size(); ++i) {
        cameras[i].f.reset();
        cameras[i].f.look_pitch = cameras[i].f.fixed_heading = false;
        cameras[i].placed = false;
    }
    cameras[kCameraOverhead].f.height = 25.0f;
    cameras[kCameraOverhead].f.pitch = -100.0f;
    UserCamera& u1 = cameras[kCameraUser1].f;
    u1.distance = 30.0f;
    u1.heading = 192.0f;
    u1.height = 5.0f;
    u1.look_pitch = true;
    UserCamera& u2 = cameras[kCameraUser2].f;
    u2.distance = 82.0f;
    u2.heading = 277.0f;
    u2.height = 18.0f;
    u2.look_pitch = true;
}

glm::vec3 wheel_follow_position(const glm::vec3& eye, const glm::vec3& back, float distance, const CameraSweep& sweep) {
    const glm::vec3 wanted = eye + back * distance;
    if (!sweep) return wanted;
    return sweep(eye, wanted, kSweepRadius).value_or(eye);
}

CameraPose update_camera(int type, GameCamera& cam, const CameraTarget& target, const CameraSweep& sweep) {
    switch (type) {
    case kCameraOverhead: return overhead(cam, target, sweep);
    case kCameraChase: return chase(cam, target, sweep);
    case kCameraUser1:
    case kCameraUser2: return user(cam, target, sweep);
    case kCameraTether: return tether(cam, target, sweep);
    default: return {cam.position, target.heading, 0.0f};
    }
}

int toggle_view_next(int type) {
    int next = (type == kCameraWheelFollow ? 0 : type) + 1;
    return next < 6 ? next : 0;
}

void select_camera(CameraSelection& s, int type) {
    if (type != kCameraWheelFollow) {
        if (s.distance > 0.0f) s.pad_distance = s.distance;
        s.distance = 0.0f;
    }
    s.type = type;
}

void select_behind(CameraSelection& s, float max_distance) {
    if (s.type == kCameraWheelFollow) return;
    s.distance = std::clamp(s.pad_distance, 1.0f, max_distance);
    s.type = kCameraWheelFollow;
}

void restore_camera(CameraSelection& s, int saved, float max_distance) {
    if (saved == kCameraWheelFollow) select_behind(s, max_distance);
    else select_camera(s, saved);
}

bool camera_active(int type) { return type == kCameraChase || type == kCameraUser1 || type == kCameraUser2; }

void center_view(int type, GameCamera& cam) {
    if (type == kCameraOverhead) set_camera_pitch(type, cam, -100.0f); // 0x9c57b8
    else if (type >= kCameraChase && type <= kCameraTether) cam.f.look_pitch = true;
}

void set_camera_pitch(int type, GameCamera& cam, float pitch) {
    cam.f.pitch = pitch;
    if (type == kCameraUser1 || type == kCameraUser2) cam.f.look_pitch = false;
}

bool camera_mouse_look(int type, GameCamera& cam, float dx, float dy, float raw_dy, float sensitivity, float rate,
                       bool underwater) {
    UserCamera& f = cam.f;
    const bool turns_player = type <= kCameraChase;
    // Sideways: the user cameras (vf2c, type 3 and up) and the tether camera turn the camera's +0x38.
    if (!turns_player) f.heading = fix_heading(f.heading + dx);
    if (type <= kCameraChase || underwater) {
        if (type == kCameraChase) {
            // The height, by the motion clamped to +-sensitivity x 0.3, times the rate (vf10).
            const float limit = sensitivity * 0.3f;
            f.height -= std::clamp(raw_dy, -limit, limit) * rate;
        } else {
            set_camera_pitch(type, cam, std::clamp(f.pitch - dy, -128.0f, 128.0f));
        }
    } else if (type == kCameraTether) {
        set_camera_pitch(type, cam, std::clamp(f.pitch - dy, -120.0f, 64.0f));
    } else {
        // The user cameras' distance (vf18), at least 3.
        const float limit = sensitivity * 0.5f;
        f.distance = std::max(3.0f, f.distance + std::clamp(raw_dy, -limit, limit));
    }
    return turns_player;
}

} // namespace hailstone::game
