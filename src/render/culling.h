#pragma once

// View-volume culling (docs/graphics.md, "Culling"): the zone's geometry is cut into chunks with a bounding box each
// (render/zone_renderer.h), and a pass draws only the chunks whose box can reach its view: the camera's frustum, a sun
// cascade's box, or the cover and water maps' box. The tests are conservative: a box is dropped only when it lies
// wholly on the outside of one of the volume's planes, so a box beside a corner of the volume may be drawn for
// nothing, but nothing that shows is ever dropped.

#include <array>

#include <glm/glm.hpp>

namespace hailstone::render {

struct Box {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

// The six planes of a view-projection's clip volume, as Vulkan clips: -w <= x <= w, -w <= y <= w, 0 <= z <= w. A
// world point p is inside when dot(plane, vec4(p, 1)) >= 0 for every plane. Perspective (the camera, reverse-Z or not:
// the planes are the volume's, whichever way the depth runs) and orthographic (the sun's cascades) alike; the planes
// are not normalised, which the sign tests below do not need.
struct Frustum {
    std::array<glm::vec4, 6> planes{};
};
Frustum frustum_from(const glm::mat4& view_proj);

// Whether an axis-aligned box can show in the volume: false only when its corner furthest along a plane's normal is
// still behind that plane.
bool box_in_frustum(const Frustum& frustum, const Box& box);

// The box round a sphere (an actor's or a door's bounds), for the tests above.
inline Box sphere_box(const glm::vec3& center, float radius) { return {center - glm::vec3(radius), center + glm::vec3(radius)}; }

// Whether two axis-aligned boxes share any point (touching counts).
bool boxes_overlap(const Box& a, const Box& b);

} // namespace hailstone::render
