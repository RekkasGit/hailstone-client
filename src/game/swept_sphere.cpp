#include "game/swept_sphere.h"

#include "game/sphere_sweep.h"

#include <algorithm>
#include <cmath>

namespace hailstone::game {

namespace {

constexpr float kValidRange = 100000.0f; // CCollisionInfoBase vf8: the world box's half size seen from the origin

} // namespace

SphereSweep SphereSweep::make(const glm::vec3& from, const glm::vec3& to, float radius) {
    sweep::Start<float> i = sweep::start<float>(from, to, radius);
    SphereSweep s;
    s.start = from;
    s.end = to;
    s.delta = i.delta;
    s.direction = i.direction;
    s.length_inverse = i.length_inverse;
    s.backfaces_ignored = i.length_inverse != 0.0f;
    s.radius = radius;
    s.radius_squared = i.radius_squared;
    s.radius_inverse = i.radius_inverse;
    return s;
}

bool SphereSweep::valid() const {
    for (int k = 0; k < 3; ++k)
        if (!(std::fabs(start[k]) < kValidRange - std::fabs(delta[k]))) return false;
    return !std::isnan(radius);
}

CollisionTriangle CollisionTriangle::make(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                                          std::uint8_t flags) {
    CollisionTriangle t;
    t.v0 = v0;
    t.v1 = v1;
    t.v2 = v2;
    t.flags = flags;
    glm::vec3 n = glm::cross(v0 - v1, v2 - v1);
    float len = glm::length(n);
    if (len > 0.0f && std::isfinite(len)) {
        t.normal = n / len;
    } else {
        t.normal = glm::vec3(0.0f); // degenerate: flag 0x10 and a zero plane
        t.flags |= 0x10;
    }
    t.radius = std::max(glm::length(v1 - v0), glm::length(v2 - v0));
    return t;
}

glm::vec3 normalize_or_zero(const glm::vec3& v) { return sweep::normalize_or_zero<float>(v); }

bool collide_triangle(SphereSweep& s, const CollisionTriangle& tri) {
    // The DLL's leaf filter (TTerrainCollisionTester<Sphere>::vf0 @0x100bda70).
    if (tri.flags & (kTrianglePassable | 0x10)) return false; // passable (or degenerate) never blocks
    if (s.backfaces_ignored && glm::dot(tri.normal, s.delta) > 0.0f) return false;
    // The floor-only mode (0x100bdbe4..0x100bdc36): n . (0, 0, 1) fucomp 0, skipped when equal (not unordered).
    // Terrain triangles only: placed objects are actors in the original, whose test does not ask slot 5.
    if (s.floor_only && !(tri.flags & kTriangleObject) && tri.normal.z == 0.0f) return false;
    // CCollisionInfoBase_CanSkipTriangle @0x100bd020 (here also less the sphere's radius, so that it only skips
    // triangles the sweep cannot reach; the DLL's own arithmetic is not ported).
    if (s.collided && s.t == 0.0f) return false;
    if (s.t < (glm::length(tri.v0 - s.start) - tri.radius - s.radius) * s.length_inverse) return false;
    // CCollisionInfoSphere::vf0 @0x7a28e0's behaviour (game/sphere_sweep.h).
    sweep::TriangleHit<float> h = sweep::sphere_triangle<float>(s.start, s.delta, s.length_inverse, s.t, s.radius,
                                                                tri.normal, tri.v0, tri.v1, tri.v2);
    if (!h.hit) return false;
    s.t = h.t;
    s.normal = h.normal;
    s.collided = true;
    return true;
}

SweepOutput sweep_output(const SphereSweep& s) {
    SweepOutput out;
    out.position = sweep::clamp_point<float>(s.start, s.delta, s.t);
    if (s.collided) {
        // s3dCollideActorSweep computes the slide only after a hit.
        sweep::Skitter<float> k = sweep::skitter<float>(s.direction, s.delta, s.t, s.normal);
        out.skitter_dir = k.direction;
        out.skitter_dist = k.distance;
    }
    return out;
}

} // namespace hailstone::game
