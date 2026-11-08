#pragma once

// The zone lights' shadow cubes in a frame (render/point_shadows.h), for the client and the viewer: plan before the
// renderers take the frame's lighting, draw after the characters are posed and before the scene.

#include <cstdint>
#include <span>
#include <vector>

#include <glm/vec3.hpp>

#include "render/point_shadows.h"
#include "render/vk_context.h"

namespace hailstone::assets { struct PointLight; }

namespace hailstone::render {

class ActorRenderer;
class ObjectRenderer;
class ZoneRenderer;
struct SceneLighting;

// Matches `shadows` to the point shadow map (its slot count, a new map empty): before PointShadows::set_dynamic,
// whose moving lights a resize would drop. plan_point_shadows does it too.
void sync_point_shadows(PointShadows& shadows, const VkContext& vk);

// Picks the slots (PointShadows::update), marks live those with characters in reach (as `actors` last posed them:
// a frame late, which does not show) or placed objects (doors, ground objects: `objects` as prepared, which can move
// too) and fills `lighting`'s point shadow fields. Returns the slots to build.
std::vector<std::uint32_t> plan_point_shadows(PointShadows& shadows, const VkContext& vk,
                                              std::span<const assets::PointLight> lights, const glm::vec3& camera,
                                              float dt, const ActorRenderer* actors, SceneLighting& lighting,
                                              const ObjectRenderer* objects = nullptr);

// Draws the zone into the cache of each slot in `builds`, then into each live slot's cube its cache, the characters
// (`actors` posed this frame) and the placed objects (`objects` prepared this frame), and into each moving light's
// cube (PointShadows::set_dynamic, before plan_point_shadows) the zone, the objects and the characters round it.
// Outside the scene's rendering.
void draw_point_shadows(VkContext& vk, const FrameInfo& frame, PointShadows& shadows,
                        std::span<const assets::PointLight> lights, std::span<const std::uint32_t> builds,
                        ZoneRenderer& zone, ActorRenderer* actors, ObjectRenderer* objects = nullptr);

} // namespace hailstone::render
