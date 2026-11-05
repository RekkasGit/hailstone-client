#include "game/view_distance.h"

#include "game/fog.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace nmsc::game {

namespace {

std::uint32_t bits(float f) { return std::bit_cast<std::uint32_t>(f); }

} // namespace

glm::vec2 set_fog_distances(float start, float end, float far_plane, bool fog_enabled, FogConditions conditions) {
    // CDisplay::SetFog (game::set_fog) with the far plane as the clip globals
    // (ClipPlane 0, minclip = maxclip = far) and as Yon.
    SetFogInput in;
    in.on = fog_enabled ? 1u : 0u;
    in.start = bits(start);
    in.end = bits(end);
    in.yon = bits(far_plane);
    in.raining = conditions.raining;
    in.camera_region = conditions.special_region ? 5 : 0;
    in.clip = {0, bits(far_plane), bits(far_plane)};
    SetFogResult r = set_fog(mechanics::Fpu(), in);
    return {static_cast<float>(r.start), static_cast<float>(r.end)};
}

ZoneFogInput new_zone_fixes(const ZoneFogInput& zone) {
    ZoneFogInput z = zone;
    if (z.fog_minclip == 0.0f && z.fog_maxclip == 0.0f) {
        z.fog_minclip = 500.0f;
        z.fog_maxclip = 600.0f;
        z.fog_color = glm::vec3(192.0f / 255.0f);
    }
    z.minclip = std::max(z.minclip, 50.0f);
    if (z.maxclip < 51.0f) z.maxclip = z.fog_maxclip * 4.0f;
    if (z.minclip >= z.maxclip) z.maxclip = z.minclip * 4.0f;
    return z;
}

float actor_clip_distance(int actor_clip_plane, float far_plane) {
    // lea eax, [edi + edi*4 + 0x19]; add eax, eax; fild; fcom against the Yon: the smaller.
    const float clip = static_cast<float>((actor_clip_plane * 5 + 25) * 2);
    return clip <= far_plane ? clip : far_plane;
}

SpawnDraw spawn_draw(bool has_actor, bool has_model, float distance, float actor_clip) {
    if (!has_actor && !has_model) return SpawnDraw::Box;
    if (!has_model) return SpawnDraw::Empty;
    if (actor_clip > 0.0f && distance > actor_clip) return SpawnDraw::Clipped;
    return SpawnDraw::Model;
}

float zone_far_plane(const ZoneFogInput& fixed, int clip_plane) {
    clip_plane = std::clamp(clip_plane, 0, 20);
    return fixed.minclip + static_cast<float>(clip_plane) * 0.05f * (fixed.maxclip - fixed.minclip);
}

ViewDistance render_view(const RenderFog& fog, float far_plane) {
    ViewDistance out;
    out.far_plane = far_plane;
    out.fog_color = glm::vec3(static_cast<float>(fog.colour >> 16 & 0xff), static_cast<float>(fog.colour >> 8 & 0xff),
                              static_cast<float>(fog.colour & 0xff)) / 255.0f;
    out.fog_enabled = fog.on != 0;
    // CRender::SetFog: end <= start becomes start + 1.
    std::int32_t end = fog.end <= fog.start ? fog.start + 1 : fog.end;
    out.fog_start = static_cast<float>(fog.start);
    out.fog_end = static_cast<float>(end);
    float density = std::bit_cast<float>(fog.density);
    out.fog_density = out.fog_enabled && std::isfinite(density) ? density : 0.0f;
    if (!out.fog_enabled) {
        out.fog_start = far_plane * 2.0f;
        out.fog_end = far_plane * 4.0f;
    }
    return out;
}

ViewDistance compute_view_distance(const ZoneFogInput& zone, int clip_plane, FogConditions conditions) {
    // OP_NewZone's SetFog call (@0x4ca05e) with the fixed values.
    ZoneFogInput z = new_zone_fixes(zone);
    float far = zone_far_plane(z, clip_plane);
    SetFogInput in;
    in.on = z.fog_enabled ? 1u : 0u;
    in.start = bits(z.fog_minclip);
    in.end = bits(z.fog_maxclip);
    in.density = bits(z.fog_density);
    in.red = static_cast<std::uint8_t>(std::lround(std::clamp(z.fog_color.r, 0.0f, 1.0f) * 255.0f));
    in.green = static_cast<std::uint8_t>(std::lround(std::clamp(z.fog_color.g, 0.0f, 1.0f) * 255.0f));
    in.blue = static_cast<std::uint8_t>(std::lround(std::clamp(z.fog_color.b, 0.0f, 1.0f) * 255.0f));
    in.yon = bits(far);
    in.raining = conditions.raining;
    in.camera_region = conditions.special_region ? 5 : 0;
    in.clip = {std::clamp(clip_plane, 0, 20), bits(z.minclip), bits(z.maxclip)};
    SetFogResult r = set_fog(mechanics::Fpu(), in);
    return render_view(*r.render, far);
}

CameraFov camera_fov(float fov_units, int width, int height) {
    constexpr double kUnitToRad = 0.703125 * std::numbers::pi / 180.0; // 360/512 degrees
    CameraFov out;
    double aspect = height > 0 ? static_cast<double>(width) / static_cast<double>(height) : 1.0;
    if (aspect < 2.0) {
        out.h = fov_units;
        out.v = static_cast<float>(fov_units / aspect);
    } else {
        // .rdata 0x9c7b34 (0.7501876), 0x9c7b24 (1.4222223 = 512/360), 0x9c58e4 (200).
        double v = static_cast<double>(fov_units) * 0.7501876;
        double a = 2.0 * std::atan(std::tan(v * kUnitToRad * 0.5) * aspect);
        double h = a * (180.0 / std::numbers::pi) * 1.4222223;
        if (h > 200.0) h = 200.0;
        out.h = static_cast<float>(h);
        out.v = static_cast<float>(v);
    }
    // CCamera::SetFieldOfView stores h/2 (+4) and h/v (+8); the projection uses
    // w = 2 tan(h/2) and a height of w / (h/v) (negated: D3D's Y flip).
    out.tan_half_x = static_cast<float>(std::tan(static_cast<double>(out.h) * kUnitToRad * 0.5));
    out.tan_half_y = out.v != 0.0f ? out.tan_half_x / (out.h / out.v) : out.tan_half_x;
    return out;
}

} // namespace nmsc::game
