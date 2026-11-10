#include "game/iso_camera.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace hailstone::game {

glm::vec3 iso_forward(float yaw, float pitch_down) {
    const float c = std::cos(pitch_down);
    return {c * std::cos(yaw), c * std::sin(yaw), -std::sin(pitch_down)};
}

IsoPose update_iso_camera(IsoCamera& cam, const glm::vec3& target, float dt, const IsoCameraSettings& s) {
    if (!cam.placed || glm::distance(cam.focus, target) > s.snap_distance || s.follow_tau <= 0.0f) {
        cam.focus = target;
        cam.placed = true;
    } else if (dt > 0.0f) {
        cam.focus += (target - cam.focus) * (1.0f - std::exp(-dt / s.follow_tau));
    }
    cam.distance = std::clamp(cam.distance, s.min_distance, s.max_distance);
    IsoPose p;
    p.yaw = cam.yaw;
    p.pitch = -s.pitch;
    p.focus_distance = cam.distance;
    p.position = cam.focus - iso_forward(cam.yaw, s.pitch) * cam.distance;
    return p;
}

void iso_zoom(IsoCamera& cam, float notches, const IsoCameraSettings& s) {
    if (notches == 0.0f) return;
    cam.distance = std::clamp(cam.distance * std::pow(1.0f - s.zoom_step, notches), s.min_distance, s.max_distance);
}

void iso_turn(IsoCamera& cam, float d_yaw) { cam.yaw = std::remainder(cam.yaw + d_yaw, glm::two_pi<float>()); }

float iso_fov_units(float degrees) { return degrees * (512.0f / 360.0f); }

ViewDistance iso_view(const ViewDistance& v, float focus_distance) {
    ViewDistance out = v;
    const float d = std::max(focus_distance, 0.0f);
    out.fog_start += d;
    out.fog_end += d;
    out.far_plane += d;
    return out;
}

float iso_shadow_near(float focus_distance, float near_plane) {
    return std::max(near_plane, focus_distance - kIsoShadowLead);
}

} // namespace hailstone::game
