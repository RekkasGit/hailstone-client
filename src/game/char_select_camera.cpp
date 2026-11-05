#include "game/char_select_camera.h"

#include <cmath>

#include "game/doors.h" // fix_heading
#include "game/follow.h"

namespace nmsc::game {

namespace {

// The float constants SetCCreateCamera reads (their addresses in eqgame.exe).
constexpr float k9c8350 = 1.4f, k9c4e4c = -0.25f, k9c834c = 1.6f, k9c58e8 = 2.0f, k9c8354 = 1.8f, k9c8348 = 2.1f;
constexpr float k9c8344 = 2.35f, k9c4a48 = 0.1f, k9c8340 = -0.3f, k9c7dd8 = 2.2f, k9c4e48 = -0.5f, k9c833c = -0.1f;
constexpr float k9c5a14 = 1.5f, k9c8338 = 1.7f, k9c8334 = 3.5f, k9c8330 = 3.8f, k9c832c = -0.6f, k9c7dc8 = 2.5f;
constexpr float k9c7da0 = 6.0f, k9c8328 = -2.5f, k9c58a8 = 1.2f, k9c4998 = 0.5f, k9c4ec4 = -1.0f, k9c7b68 = 1.3f;
constexpr float k9c5764 = 4.0f, k9c3920 = 3.0f, k9c8324 = -1.2f, k9c8320 = -0.4f, k9c831c = 2.4f, k9c7dcc = 7.0f;
constexpr float k9c59fc = 0.33f;
constexpr float kFovSelect = 90.0f; // 0x9c735c
constexpr float kFovCreate = 70.0f; // 0x9c8358

// The race switch @0x495286: the old model's camera by race and gender (the setters +0x10 height, +0x18 distance).
void race_heights(UserCamera& cam, std::int32_t race, std::uint8_t gender) {
    switch (race) {
    case 1:
        if (gender == 0) cam.distance = k9c8350;
        break;
    case 8:
        if (gender == 0) cam.height = k9c4e4c;
        else cam.distance = k9c834c;
        break;
    case 6: cam.distance = k9c834c; break;
    case 3: cam.distance = gender != 0 ? k9c58e8 : k9c8354; break;
    case 128:
        if (gender == 1) {
            cam.distance = k9c8348;
            cam.height = 0.0f;
        } else {
            cam.distance = k9c8344;
            cam.height = k9c4a48;
        }
        break;
    case 2: cam.distance = k9c8348; break;
    case 11:
        if (gender == 1) cam.distance = k9c8354;
        break;
    case 9:
        if (gender == 1) {
            cam.height = k9c8340;
            cam.distance = k9c7dd8;
        } else {
            cam.height = k9c4e48;
        }
        break;
    case 12:
        cam.height = k9c833c;
        cam.distance = k9c5a14;
        break;
    case 7: cam.distance = k9c8338; break;
    case 10: cam.distance = gender == 0 ? k9c8334 : k9c8330; break;
    case 130:
        cam.height = k9c832c;
        cam.distance = k9c7dc8;
        break;
    case 330:
        cam.distance = k9c7da0;
        cam.height = k9c8328;
        break;
    default: break;
    }
}

} // namespace

void UserCamera::reset() {
    c28 = height = pitch = distance = heading = c3c = 0.0f;
    fov = 90.0f;
}

namespace {
float table_entry(float heading, bool sine) {
    // CMath::InitSinCosTables: cos[i] = (float)cos(i pi / 256), sin[j] = cos[j - 128], the quadrants exact.
    int i = static_cast<int>(std::trunc(heading)) & 511;
    if (sine) i = (i + 384) & 511; // sin[j] = cos[j - 128]
    switch (i) {
    case 0: return 1.0f;
    case 128: return 0.0f;
    case 256: return -1.0f;
    case 384: return 0.0f;
    default: return static_cast<float>(std::cos(static_cast<double>(i) * 3.14159265358979323846 / 256.0));
    }
}
} // namespace

float table_sin(float heading) { return table_entry(heading, true); }
float table_cos(float heading) { return table_entry(heading, false); }

UserCameraPose user_camera_update(UserCamera& cam, const UserCameraTarget& target, bool in_game) {
    float eye_z = target.position.z + target.eye_height;
    if (target.flag_35c) eye_z = target.position.z + 1.5f;
    if (in_game) {
        if (!(cam.distance >= 2.0f)) cam.distance = 2.0f;
        else if (200.0f < cam.distance) cam.distance = 200.0f;
        if (!(cam.height >= 0.0f)) cam.height = 0.0f;
        else if (30.0f < cam.height) cam.height = 30.0f;
    }
    cam.heading = fix_heading(cam.heading);
    UserCameraPose pose;
    pose.position.x = table_cos(cam.heading) * cam.distance + target.position.x;
    pose.position.y = table_sin(cam.heading) * cam.distance + target.position.y;
    pose.position.z = cam.height + eye_z;
    pose.heading = cam.fixed_heading ? 0.0f : static_cast<float>(x87_bearing(pose.position.x, pose.position.y,
                                                                              target.position.x, target.position.y,
                                                                              mechanics::Fpu{}));
    // +0x44: the pitch toward the target's eye (the table's arctangent); never set by the screens that use this.
    pose.pitch = cam.pitch;
    return pose;
}

CCreateResult set_ccreate_camera(UserCamera& cam, int mode, const CCreateInputs& in) {
    CCreateResult r;
    const std::int32_t race = in.race;
    const std::uint8_t g = in.gender;
    if (mode == 0) {
        r.fov = kFovSelect;
        cam.distance = (race == 10 || race == 9 || race == 128) ? k9c7dcc : k9c7da0;
        cam.height = -(in.view_height * k9c59fc);
        cam.distance = in.size + k9c58e8 + cam.distance;
        return r;
    }
    if (mode != 100) r.fov = kFovCreate;
    if (in.new_style_model) {
        cam.distance = in.ccc_distance;
        cam.height = in.ccc_height;
        return r;
    }
    if (in.model_settings) {
        r.animation = 0x47;
        cam.distance = k9c8354;
        cam.height = 0.0f;
        race_heights(cam, race, g);
        if (mode != 2) return r;
        if (in.can_be_bald != 1) {
            cam.heading = in.heading; // the setter +0x20 with the player's +0x80
            return r;
        }
        bool face = (race == 1 && g == 0) || (race == 10 && g == 1);
        cam.distance = cam.distance + (face ? k9c58a8 : k9c4998);
        return r;
    }
    // No model settings.
    r.animation = race == 130 ? 0x47 : 0x20;
    float h;
    if (g != 0) h = (race == 10 || race == 9) ? k9c4ec4 : k9c4e48;
    else if (race == 10 || race == 9) h = k9c4ec4;
    else if (race == 8 || race == 11 || race == 12) h = k9c4e48;
    else h = 0.0f;
    cam.height = h;
    cam.distance = (g != 0 && race != 3 && race != 5) ? k9c7b68 : k9c5a14;
    if (race == 8 || race == 12 || race == 11) cam.distance = g == 0 ? k9c7dc8 : k9c5a14;
    if (race == 10 || race == 9) {
        cam.distance = g == 0 ? k9c5764 : k9c3920;
        if (race == 10 && g != 0) cam.distance = k9c5764;
        return r;
    }
    if (race == 128) {
        cam.distance = k9c7dc8;
        cam.height = k9c4998;
        return r;
    }
    if (race == 130) {
        cam.height = g == 1 ? k9c8324 : k9c8320;
        cam.distance = k9c831c;
        return r;
    }
    if (race == 436) cam.distance = k9c5764;
    return r;
}

void start_network_game_camera(UserCamera& cam, float view_height) {
    cam.distance = 6.0f;
    cam.height = -(view_height * k9c59fc);
    cam.heading = 456.0f;
    cam.pitch = 0.0f;
    cam.c28 = 0.0f;
    cam.fov = 90.0f;
}

CCreateResult face_camera(UserCamera& cam, const CCreateInputs& in, bool froglok_raise) {
    cam.reset();
    CCreateResult r = set_ccreate_camera(cam, 100, in);
    cam.heading = in.heading;
    cam.distance = cam.distance + 1.5f;
    cam.pitch = 0.0f;
    if (froglok_raise && in.race == 330) cam.height = cam.height + 1.5f;
    return r;
}

CCreateResult customization_camera(UserCamera& cam, const CustomizationCameraCall& call, const CCreateInputs& in) {
    return call.face ? face_camera(cam, in, call.raise) : set_ccreate_camera(cam, call.mode, in);
}

void char_select_camera_tick(UserCamera& cam, float model_heading) { cam.heading = model_heading; }

CCreateResult char_create_camera_tick(UserCamera& cam, int customization_page, const CCreateInputs& in) {
    if (customization_page == 2 || (customization_page == 3 && in.new_style_model)) {
        CCreateResult r = set_ccreate_camera(cam, customization_page, in);
        if (in.can_be_bald == 1) cam.heading = cam.heading + 1.5f;
        return r;
    }
    cam.heading = in.heading;
    return {};
}

} // namespace nmsc::game
