#pragma once

// Cascaded shadow maps for the modern lighting's sun or moon (docs/graphics.md): the camera's view, out to the shadow
// distance, is cut into kShadowCascades slices; each slice gets an orthographic view from the light that covers the
// slice's bounding sphere, snapped to whole texels so the shadows do not shimmer as the camera moves.

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

namespace hailstone::render {

inline constexpr std::uint32_t kShadowCascades = 4;
// The shadow map's layers: the cascades, then the cover map and the water map (render/cover_map.h), the zone's solid
// geometry and its water surfaces seen from straight above.
inline constexpr std::uint32_t kCoverLayer = kShadowCascades;
inline constexpr std::uint32_t kWaterLayer = kShadowCascades + 1;
inline constexpr std::uint32_t kShadowLayers = kShadowCascades + 2;

struct ShadowCamera {
    glm::vec3 position{0.0f};
    glm::vec3 forward{1.0f, 0.0f, 0.0f};
    glm::vec3 right{0.0f, -1.0f, 0.0f};
    glm::vec3 up{0.0f, 0.0f, 1.0f};
    float tan_half_x = 1.0f; // the projection's half angles
    float tan_half_y = 1.0f;
    float near_plane = 1.0f;
};

struct ShadowCascades {
    std::array<glm::mat4, kShadowCascades> view_proj{}; // world to the cascade's clip space (depth 0..1)
    glm::vec4 splits{0.0f};       // the far view depth (along `forward`) of each cascade
    glm::vec4 texel_world{0.0f};  // the world size of one shadow-map texel in each cascade
};

// `to_light`: unit vector towards the light. `distance`: how far from the camera shadows are drawn. `caster_reach`:
// how far towards the light a caster can stand outside a slice and still shadow it. `map_size`: texels per side.
ShadowCascades fit_shadow_cascades(const ShadowCamera& camera, const glm::vec3& to_light, float distance,
                                   float caster_reach, std::uint32_t map_size);

// How far from the camera the modern lighting draws shadows, and how far a caster may stand outside a slice.
inline constexpr float kShadowDistance = 1200.0f;
inline constexpr float kShadowCasterReach = 2000.0f;

} // namespace hailstone::render
