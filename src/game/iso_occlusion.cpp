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

float iso_ease(float current, float target, float dt) {
    if (dt <= 0.0f) return current;
    if (target > current) return std::min(target, current + dt / kIsoEaseIn);
    return std::max(target, current - dt / kIsoEaseOut);
}

IsoReveal iso_reveal(const IsoRevealInput& in) {
    IsoReveal r;
    const glm::vec3 to = in.focus - in.camera;
    const float distance = glm::length(to);
    const float cone = in.radius > 0.0f ? std::clamp(in.strength, 0.0f, 1.0f) : 0.0f;
    const float roof = in.roof_radius > 0.0f ? std::clamp(in.roof_strength, 0.0f, 1.0f) : 0.0f;
    if (distance < 1e-3f || (cone <= 0.0f && roof <= 0.0f)) return r; // off
    r.origin = glm::vec4(in.camera, cone);
    r.axis = glm::vec4(to / distance, distance);
    r.shape = glm::vec4(std::max(in.radius, 0.0f), in.feet_z + kIsoRevealFloor, kIsoRevealMargin, kIsoRevealSoft);
    r.roof = glm::vec4(in.roof_height, in.roof_radius, kIsoRoofSoft, roof);
    return r;
}

namespace {
float cone_cut(const IsoReveal& r, const glm::vec3& p) {
    if (r.origin.w <= 0.0f) return 0.0f;
    const glm::vec3 v = p - glm::vec3(r.origin);
    const glm::vec3 axis(r.axis);
    const float t = glm::dot(v, axis);
    if (t <= 0.0f || t >= r.axis.w - r.shape.z) return 0.0f;
    // The distance from the line, as it shows at the player's depth: a cone, so a circle on the screen.
    const float lateral = glm::length(v - t * axis) * r.axis.w / t;
    const float edge = 1.0f - smoothstep(r.shape.x * (1.0f - r.shape.w), r.shape.x, lateral);
    const float height = smoothstep(r.shape.y, r.shape.y + kIsoRevealRise, p.z);
    const float depth = 1.0f - smoothstep(r.axis.w - r.shape.z - kIsoRevealDepthFade, r.axis.w - r.shape.z, t);
    return r.origin.w * edge * height * depth;
}

float roof_cut(const IsoReveal& r, const glm::vec3& p) {
    if (r.roof.w <= 0.0f) return 0.0f;
    const glm::vec3 player = glm::vec3(r.origin) + glm::vec3(r.axis) * r.axis.w;
    const float flat = glm::length(glm::vec2(p) - glm::vec2(player));
    const float edge = 1.0f - smoothstep(r.roof.y - r.roof.z, r.roof.y, flat);
    const float height = smoothstep(r.roof.x, r.roof.x + kIsoRoofRise, p.z);
    return r.roof.w * edge * height;
}
} // namespace

float iso_reveal_cut(const IsoReveal& r, const glm::vec3& p) { return std::max(cone_cut(r, p), roof_cut(r, p)); }

bool iso_pick_skips(const IsoReveal& r, const PickSegment& s, const PickTriangle& t) {
    if (r.origin.w <= 0.0f && r.roof.w <= 0.0f) return false;
    // The pick's own hit test (game/picking.h): the segment's closest point on the triangle, touching it.
    const SegmentTriangleDistance d = segment_triangle_distance(s, t.v0, t.v1, t.v2);
    if (d.distance_squared > kPickHitDistanceSquared) return false;
    return iso_reveal_cut(r, s.start + s.dir * d.t) >= 0.5f;
}

} // namespace hailstone::game
