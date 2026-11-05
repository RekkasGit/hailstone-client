#include "render/cover_map.h"

#include <cmath>

#include "render/scene_lighting.h"

namespace nmsc::render {

CoverMap fit_cover_map(const glm::vec3& camera, std::uint32_t map_size) {
    CoverMap out;
    float size = 2.0f * kCoverHalfSize;
    float texel = size / static_cast<float>(map_size > 0 ? map_size : 1u);
    float x0 = std::floor((camera.x - kCoverHalfSize) / texel) * texel;
    float y0 = std::floor((camera.y - kCoverHalfSize) / texel) * texel;
    // The top at or above kCoverAbove over the camera, the bottom at or below kCoverBelow under it.
    float top = std::ceil((camera.z + kCoverAbove) / kCoverStep) * kCoverStep;
    out.cover = glm::vec4(x0, y0, 1.0f / size, top);
    out.params = glm::vec4(1.0f / (kCoverAbove + kCoverBelow + kCoverStep), 0.0f, 0.0f, 0.0f);
    return out;
}

glm::vec4 cover_clip(const CoverMap& map, const glm::vec3& p) {
    glm::vec2 uv = (glm::vec2(p) - glm::vec2(map.cover)) * map.cover.z;
    return glm::vec4(uv * 2.0f - 1.0f, (map.cover.w - p.z) * map.params.x, 1.0f);
}

glm::mat4 cover_view_proj(const glm::vec4& cover, const glm::vec4& cover_params) {
    glm::mat4 m(0.0f);
    float s = 2.0f * cover.z;
    m[0][0] = s;
    m[1][1] = s;
    m[2][2] = -cover_params.x;
    m[3] = glm::vec4(-s * cover.x - 1.0f, -s * cover.y - 1.0f, cover.w * cover_params.x, 1.0f);
    return m;
}

Box cover_box(const glm::vec4& cover, const glm::vec4& cover_params) {
    float size = cover.z > 0.0f ? 1.0f / cover.z : 0.0f;
    float depth = cover_params.x > 0.0f ? 1.0f / cover_params.x : 0.0f;
    return {glm::vec3(cover.x, cover.y, cover.w - depth), glm::vec3(cover.x + size, cover.y + size, cover.w)};
}

void set_cover(SceneLighting& lighting, const CoverMap& map, bool cover, bool water) {
    lighting.cover = map.cover;
    lighting.cover_params = glm::vec4(map.params.x, cover ? 1.0f : 0.0f, water ? 1.0f : 0.0f, 0.0f);
}

std::uint64_t cover_contents(std::uint64_t zone, std::uint32_t shadow_map, std::uint64_t casters) {
    std::uint64_t h = casters;
    for (std::uint64_t v : {zone, std::uint64_t{shadow_map}}) h = (h ^ v) * 1099511628211ull + 0x9e3779b97f4a7c15ull;
    return h;
}

bool cover_map_stale(CoverMapState& state, bool on, const CoverMap& map, std::uint64_t contents, bool moving) {
    if (!on) {
        state.valid = false;
        return false;
    }
    const bool same = state.valid && state.map.cover == map.cover && state.map.params.x == map.params.x &&
                      state.contents == contents && !moving;
    state = {true, map, contents};
    return !same;
}

} // namespace nmsc::render
