#pragma once

// Bounding spheres of shadow casters placed with a model matrix (render/object_renderer.h: doors, ground objects),
// for the tests that pick which casters a shadow draws: a light's reach (render/point_shadows.h), a cube's face.

#include <algorithm>
#include <span>

#include <glm/glm.hpp>

namespace hailstone::render {

struct BoundingSphere {
    glm::vec3 center{0.0f};
    float radius = 0.0f;
};

// The sphere round a model's vertex positions: the centre of their box, out to the farthest of them.
inline BoundingSphere bounding_sphere(std::span<const glm::vec3> points) {
    if (points.empty()) return {};
    glm::vec3 lo = points.front(), hi = points.front();
    for (const glm::vec3& p : points) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    BoundingSphere s{(lo + hi) * 0.5f, 0.0f};
    for (const glm::vec3& p : points) s.radius = std::max(s.radius, glm::length(p - s.center));
    return s;
}

// A model-space sphere under `transform`: its centre moved, its radius grown by the largest of the matrix's axis
// scales, so a stretched or scaled door's sphere still holds all of it.
inline BoundingSphere transform_sphere(const glm::mat4& transform, const BoundingSphere& s) {
    float scale = std::max({glm::length(glm::vec3(transform[0])), glm::length(glm::vec3(transform[1])),
                            glm::length(glm::vec3(transform[2]))});
    return {glm::vec3(transform * glm::vec4(s.center, 1.0f)), s.radius * scale};
}

// Whether a caster's sphere reaches into a light's (the light at `light`, reaching `radius`).
inline bool sphere_in_reach(const BoundingSphere& s, const glm::vec3& light, float radius) {
    return glm::length(s.center - light) < radius + s.radius;
}

} // namespace hailstone::render
