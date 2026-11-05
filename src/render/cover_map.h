#pragma once

// The cover map (docs/graphics.md, "Weather"): the zone round the camera seen from straight above, a depth map in the
// shadow map's kCoverLayer, so the rain and snow can tell what is out in the open. Rain does not fall, and the ground
// does not get wet or snowed on, under anything the map shows overhead: roofs, bridges, overhangs and tree canopies,
// the doors' and ground objects' too, within 250 units (shaders/lighting.glsl kShelterReach).
// An orthographic view looking down, its texels snapped to the world so its edges do not crawl as the camera moves,
// its top snapped to kCoverStep so that it stays the same while the camera moves up and down a little.
//
// A map is drawn again only when its view or what it shows has changed (CoverMapState): its layer of the shadow map
// keeps its depths between frames, since nothing else draws into it.
//
// The water map (kWaterLayer) is the same view of the zone's water surfaces alone: how deep a point lies under water,
// for the caustics on what is under it and the light shafts when the camera is under it (shaders/lighting.glsl
// water_depth).

#include <cstdint>

#include <glm/glm.hpp>

#include "render/culling.h"

namespace nmsc::render {

struct SceneLighting;

inline constexpr float kCoverHalfSize = 256.0f; // the map reaches this far from the camera each way
inline constexpr float kCoverAbove = 1000.0f;   // and this far above the camera
inline constexpr float kCoverBelow = 1500.0f;   // and below
inline constexpr float kCoverStep = 32.0f;      // its top is a multiple of this, its depth range that much longer

struct CoverMap {
    glm::vec4 cover{0.0f};  // x, y: the map's corner, z: 1 / its size, w: its top (SceneLighting::cover)
    glm::vec4 params{0.0f}; // x: 1 / its depth range (SceneLighting::cover_params; set_cover adds the flags)
};

CoverMap fit_cover_map(const glm::vec3& camera, std::uint32_t map_size);

// A world point's place in the map's clip space (shaders/zone_shadow.vert does the same).
glm::vec4 cover_clip(const CoverMap& map, const glm::vec3& p);

// The same as a matrix, for what is drawn into the map with its own vertex shader (render::ObjectRenderer).
glm::mat4 cover_view_proj(const glm::vec4& cover, const glm::vec4& cover_params);

// The world box the map sees, from its fields as SceneLighting holds them (cover, cover_params): its square of ground,
// from its top down through its depth range. What lies outside draws nowhere in it (render::ZoneRenderer culls by it).
Box cover_box(const glm::vec4& cover, const glm::vec4& cover_params);

// The view into the lighting, with which of the two maps are on this frame.
void set_cover(SceneLighting& lighting, const CoverMap& map, bool cover, bool water);

// What one of the maps showed when it was last drawn.
struct CoverMapState {
    bool valid = false;
    CoverMap map;
    std::uint64_t contents = 0;
};

// Whether a map must be drawn this frame; notes what it then shows. `on`: the map is used this frame (a map that is
// not drawn one frame is drawn afresh the next it is on). `contents`: what is drawn into it and into what (the zone,
// the shadow map, the casters as they stand); `moving`: something in its view moves each frame (the plants' sway).
bool cover_map_stale(CoverMapState& state, bool on, const CoverMap& map, std::uint64_t contents, bool moving);
// cover_map_stale's `contents` from the zone renderer's id, the shadow map's generation and the casters' signature.
std::uint64_t cover_contents(std::uint64_t zone, std::uint32_t shadow_map, std::uint64_t casters);

} // namespace nmsc::render
