#pragma once

// The classic lighting constants the original's region shaders use, per frame.
// Source: rof2decomp notes/eqgraphicsdx9.md (Lighting) and the eqgame side:
// CDisplay_UpdateSkyWeatherAndFog @0x4942b0, CDisplay_UpdateSunLighting
// @0x496af0, CDisplay_UpdateSkyTimeOfDay @0x496d80.
//
// The shipped RenderEffects/SPL/RegionC1 and RegionOldA vertex shaders compute,
// per vertex, with C the WLD vertex colour (0xAARRGGBB as a D3DCOLOR) and N the
// vertex normal:
//
//   rgb = C.rgb + C.a * Ambient + SpecialAmbient
//       + C.a * (Bounce * max(N.Dn, 0) + Directional * max(N.-Dn, 0))
//   colour = texture * saturate(rgb)
//
//       + sum_i PL_i.rgb * saturate(N . normalize(P_i - pos)) * (1 - min(|P_i - pos|^2 / r_i^2, 1))
// The point lights PL_i are zero in the single-pass path. They reach shading
// only in the multi-pass "LitBatches/MPL" path (0x1009a3a0 -> 0x10099fc0):
// one additive pass per light, run when eqclient.ini MultiPassLighting is on
// and the material's effect has an MPL triple (point_light_term).

#include <cstdint>
#include <optional>

#include <glm/glm.hpp>

#include "game/sine_table.h"

namespace nmsc::game {

struct SceneLight {
    glm::vec3 ambient{0.1f};         // CSceneGraph +0x240 (vtable +0xc8)
    glm::vec3 special_ambient{0.0f}; // +0x24c (vtable +0xcc) + +0x258 (vtable +0xd0), each channel <= 1
    glm::vec3 directional{0.0f};     // the sun or moon (CDirectionalLight at +0x230)
    glm::vec3 direction{-0.70710677f, 0.0f, -0.70710677f}; // Dn; SUN_LIGHTDEF's initial direction
    glm::vec3 bounce{0.0f};          // +0x268 (vtable +0xd4)
    // The ambient's raise by infravision or ultravision over the plain floor (game::SceneFogFrame::vision_lift):
    // the modern lighting adds it, weighted as the region shader weighs the ambient (it has no ambient of its own).
    glm::vec3 vision_lift{0.0f};
};

struct SceneLightInput {
    int time_type = 0;     // OP_NewZone time_type (__ZoneType): 1, 2, 5 outdoor; 4 half lit
    float hour = 12.0f;    // the sky clock: server hour 1..24 plus minutes / 60 (game::SkyClock)
    // The player's light level L (CActor::GetScaledAmbient, smoothed); 1 with
    // no player actor, as the original's no-actor path.
    float player_light = 1.0f;
    // The vision floor: 0.08 grey, or the infravision (SPA 65) / ultravision
    // (SPA 66) colour.
    glm::vec3 vision_floor{0.08f};
    // The zone's WLD 0x35 global ambient (byte / 255), added into SpecialAmbient.
    glm::vec3 global_ambient{0.0f};
    // The per-frame scene graph colours CDisplay_UpdateSkyWeatherAndFog set
    // (+0xc8 ambient, +0xcc special ambient; 0xAARRGGBB, game::SceneFog).
    // When given they replace the ambient and the floor term above.
    std::optional<std::uint32_t> ambient_colour;
    std::optional<std::uint32_t> special_colour;
    glm::vec3 vision_lift{0.0f}; // passed through to SceneLight::vision_lift
};

SceneLight compute_scene_light(const SceneLightInput& in);

// One point light's term of the region shaders at a vertex (position `pos`,
// unit normal `n`): colour * saturate(n . normalize(light - pos)) *
// (1 - min(|light - pos|^2 / radius^2, 1)). Zero for a radius <= 0.
glm::vec3 point_light_term(const glm::vec3& light, const glm::vec3& color, float radius, const glm::vec3& pos,
                           const glm::vec3& n);

// CSkyBase::IsNight @0x100c20a0 for the day fraction hour / 24.
bool sky_is_night(float hour);

// The direction of the sun (or moon) for a sky-object heading h (512ths of a
// turn): (0, sin(512 - h), -cos(512 - h)), with the math object's
// interpolating sin and cos (CMath vtable +0x00 / +0x04).
glm::vec3 sky_light_direction(float heading);

} // namespace nmsc::game
