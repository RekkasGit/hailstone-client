#include "game/iso_occlusion.h"

#include <algorithm>
#include <cmath>

namespace hailstone::game {

namespace {
float smoothstep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
} // namespace

IsoReveal iso_reveal(const IsoRevealInput& in) {
    IsoReveal r;
    const glm::vec3 to = in.focus - in.camera;
    const float distance = glm::length(to);
    if (distance < 1e-3f || in.radius <= 0.0f || in.strength <= 0.0f) return r; // off
    r.origin = glm::vec4(in.camera, std::min(in.strength, 1.0f));
    r.axis = glm::vec4(to / distance, distance);
    r.shape = glm::vec4(in.radius, in.feet_z + kIsoRevealFloor, kIsoRevealMargin, kIsoRevealSoft);
    return r;
}

float iso_reveal_cut(const IsoReveal& r, const glm::vec3& p) {
    if (r.origin.w <= 0.0f) return 0.0f;
    const glm::vec3 v = p - glm::vec3(r.origin);
    const glm::vec3 axis(r.axis);
    const float t = glm::dot(v, axis);
    if (t <= 0.0f || t >= r.axis.w - r.shape.z) return 0.0f;
    // The distance from the line, as it shows at the player's depth: a cone, so a circle on the screen.
    const float lateral = glm::length(v - t * axis) * r.axis.w / t;
    const float edge = 1.0f - smoothstep(r.shape.x * (1.0f - r.shape.w), r.shape.x, lateral);
    const float height = smoothstep(r.shape.y, r.shape.y + 1.0f, p.z);
    return r.origin.w * edge * height;
}

} // namespace hailstone::game
