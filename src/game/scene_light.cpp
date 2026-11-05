#include "game/scene_light.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "assets/sky.h"

namespace nmsc::game {

namespace {

float fix_heading(float h) {
    h = std::fmod(h, 512.0f);
    return h < 0.0f ? h + 512.0f : h;
}

// The original stores each channel as a byte, truncated (fistp under a chop
// control word), and the graphics DLL converts back with x 1/255.
glm::vec3 through_bytes(glm::vec3 c) {
    return glm::floor(glm::clamp(c, 0.0f, 1.0f) * 255.0f) / 255.0f;
}

glm::vec3 colour_of(std::uint32_t c) {
    return glm::vec3(static_cast<float>(c >> 16 & 0xff), static_cast<float>(c >> 8 & 0xff), static_cast<float>(c & 0xff)) *
           (1.0f / 255.0f);
}

} // namespace

bool sky_is_night(float hour) {
    // CDisplay::GetSkyTime's hour (1..24) * 60 + minute, / 1440.
    float f = hour * 60.0f * (1.0f / 1440.0f);
    return f < 0.24652778f || f > 0.7638889f;
}

glm::vec3 sky_light_direction(float heading) {
    float a = 512.0f - heading;
    return {0.0f, cmath_sin_interp(a), -cmath_cos_interp(a)};
}

SceneLight compute_scene_light(const SceneLightInput& in) {
    SceneLight out;
    const bool outdoor = in.time_type == 1 || in.time_type == 2 || in.time_type == 5;

    // Ambient (CDisplay_UpdateSkyWeatherAndFog): outdoor zones take the sky
    // object's ambient colour or, without one, the per-hour tables
    // 0xaf3260/0xaf32c0/0xaf3320 interpolated by minute (assets::sky_ambient);
    // type 4 is 0.5 grey, every other type 0.1 grey. Each channel is then
    // raised to the vision floor.
    glm::vec3 ambient = outdoor ? assets::sky_ambient(in.hour) : glm::vec3(in.time_type == 4 ? 0.5f : 0.1f);
    ambient = glm::min(ambient, glm::vec3(1.0f));
    ambient = glm::max(ambient, in.vision_floor);
    out.ambient = in.ambient_colour ? colour_of(*in.ambient_colour) : through_bytes(ambient);

    // SpecialAmbient: floor x (1 - L) (vtable +0xcc) plus the zone's global
    // ambient (vtable +0xd0), each channel clamped to 1 by the DLL.
    float l = std::clamp(in.player_light, 0.0f, 1.0f);
    glm::vec3 floor_term = in.special_colour ? colour_of(*in.special_colour) : through_bytes(in.vision_floor * (1.0f - l));
    out.special_ambient = glm::min(floor_term + in.global_ambient, glm::vec3(1.0f));
    out.vision_lift = in.vision_lift;

    // The sun or moon (CDisplay_UpdateSunLighting): outdoor zones only.
    if (outdoor) {
        float sun = assets::sky_sweep_heading(in.hour);
        bool night = sky_is_night(in.hour);
        float heading = night ? fix_heading(sun + 256.0f) : sun;
        out.direction = sky_light_direction(heading);
        // The sky object's sun/moon colour (CSky vtable +0x30/+0x34) and
        // bounce (+0x3c/+0x40) come from its colour maps, which are not
        // decoded. Without a sky object the original gives SUN_LIGHTDEF the
        // sky light level (0xaf3170[level]) as a grey; that stands in for the
        // colour, and the bounce stays 0 (docs/unknowns.md).
        // The level is CDisplay's byte +0x151: whole 12-minute slots.
        out.directional = glm::vec3(assets::sky_light_level(std::floor(assets::sky_level(in.hour))));
        out.bounce = glm::vec3(0.0f);
    } else {
        out.directional = glm::vec3(0.0f);
        out.bounce = glm::vec3(0.0f);
    }
    return out;
}

glm::vec3 point_light_term(const glm::vec3& light, const glm::vec3& color, float radius, const glm::vec3& pos,
                           const glm::vec3& n) {
    if (!(radius > 0.0f)) return glm::vec3(0.0f);
    glm::vec3 d = light - pos;
    float d2 = glm::dot(d, d);
    float falloff = 1.0f - std::min(d2 / (radius * radius), 1.0f);
    if (falloff <= 0.0f) return glm::vec3(0.0f);
    float facing = d2 > 0.0f ? std::clamp(glm::dot(n, d / std::sqrt(d2)), 0.0f, 1.0f) : 1.0f;
    return color * facing * falloff;
}

} // namespace nmsc::game
