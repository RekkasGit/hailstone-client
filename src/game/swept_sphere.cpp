#include "game/swept_sphere.h"

#include "game/sweep_x87.h"

#include <algorithm>
#include <cmath>

namespace nmsc::game {

namespace {

constexpr float kValidRange = 100000.0f; // CCollisionInfoBase vf8: the world box's half size seen from the origin

// The exact x87 routines (game/sweep_x87.h), at the in-game 24-bit precision.
SweepX87& machine() {
    thread_local SweepX87 m(mechanics::FpuPrecision::Single24);
    return m;
}

std::array<float, 3> arr(const glm::vec3& v) { return {v.x, v.y, v.z}; }
glm::vec3 vec(const std::array<float, 3>& a) { return {a[0], a[1], a[2]}; }

SweepX87::Info info_of(const SphereSweep& s) {
    SweepX87::Info i;
    i.start = arr(s.start);
    i.end = arr(s.end);
    i.delta = arr(s.delta);
    i.direction = arr(s.direction);
    i.inv_length = s.length_inverse;
    i.t = s.t;
    i.normal = arr(s.normal);
    i.hit = s.collided;
    i.radius = s.radius;
    i.radius_squared = s.radius_squared;
    i.radius_inverse = s.radius_inverse;
    return i;
}

} // namespace

SphereSweep SphereSweep::make(const glm::vec3& from, const glm::vec3& to, float radius) {
    SweepX87::Info i = machine().make_info(arr(from), arr(to), radius);
    SphereSweep s;
    s.start = vec(i.start);
    s.end = vec(i.end);
    s.delta = vec(i.delta);
    s.direction = vec(i.direction);
    s.length_inverse = i.inv_length;
    s.backfaces_ignored = i.inv_length != 0.0f;
    s.radius = i.radius;
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

glm::vec3 normalize_or_zero(const glm::vec3& v) {
    float len = glm::length(v);
    if (len > -0x1p-23f && len < 0x1p-23f) return glm::vec3(0.0f);
    return v * (1.0f / len);
}

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
    // CCollisionInfoSphere::vf0 @0x7a28e0, exactly.
    SweepX87::Info i = info_of(s);
    if (!machine().triangle(i, arr(tri.normal), arr(tri.v0), arr(tri.v1), arr(tri.v2))) return false;
    s.t = i.t;
    s.normal = vec(i.normal);
    s.collided = true;
    return true;
}

SweepOutput sweep_output(const SphereSweep& s) {
    SweepX87::Info i = info_of(s);
    SweepOutput out;
    out.position = vec(machine().clamp_point(i));
    if (s.collided) {
        machine().skitter(i); // s3dCollideActorSweep computes the slide only after a hit
        out.skitter_dir = vec(i.slide);
        out.skitter_dist = i.slide_distance;
    }
    return out;
}

} // namespace nmsc::game
