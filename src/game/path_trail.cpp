#include "game/path_trail.h"

#include <algorithm>
#include <cmath>

namespace hailstone::game {

namespace {

float smooth(float edge0, float edge1, float x) {
    if (edge1 <= edge0) return x >= edge1 ? 1.0f : 0.0f;
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

std::vector<TrailSprite> build_trail(std::span<const glm::vec3> line, const TrailStyle& st, double time,
                                     const TrailFloorFn& floor, std::size_t max_sprites) {
    std::vector<TrailSprite> out;
    if (line.size() < 2 || max_sprites == 0) return out;
    const float spacing = std::max(st.spacing, 0.25f);
    const float band_len = std::max(st.band_length, 0.5f);
    // The bands' phase: wrapped so a long session's time keeps its precision.
    const double cycle = static_cast<double>(band_len) / std::max(static_cast<double>(st.band_speed), 0.001);
    const float shift = static_cast<float>(std::fmod(time, cycle) / cycle) * band_len;
    auto put_on_floor = [&](glm::vec3 p) {
        if (floor)
            if (auto f = floor(p + glm::vec3(0.0f, 0.0f, 3.0f)); f && std::abs(*f - p.z) <= 6.0f) p.z = *f;
        p.z += st.lift;
        return p;
    };

    float along = 0.0f;          // distance from the player at the current segment's start
    float next = st.near_start;  // the next sample's distance
    for (std::size_t i = 0; i + 1 < line.size() && out.size() < max_sprites; ++i) {
        const glm::vec3 a = line[i], b = line[i + 1];
        const float len = glm::length(b - a);
        if (!std::isfinite(len)) break;
        while (next <= along + len && out.size() < max_sprites) {
            if (next > st.far_end) break;
            const glm::vec3 p = len > 0.0f ? a + (b - a) * ((next - along) / len) : a;
            // the bands: soft peaks moving from the player towards the destination
            const float phase = (next - shift) / band_len;
            const float wave = std::pow(0.5f + 0.5f * std::cos(phase * 6.2831853f), 6.0f);
            const float fade = smooth(st.near_start, st.near_end, next) * (1.0f - smooth(st.far_start, st.far_end, next));
            TrailSprite s;
            s.position = put_on_floor(p);
            s.size = st.size * (1.0f + 0.5f * wave);
            s.alpha = std::clamp(st.alpha * (1.0f + st.band_boost * wave) * fade, 0.0f, 1.0f);
            if (s.alpha > 0.01f) out.push_back(s);
            next += spacing;
        }
        along += len;
        if (next > st.far_end) break;
    }
    // The column at the end, wherever the trail itself has faded: rings of light rising and fading, and a glow at
    // its foot that breathes.
    if (st.beacon) {
        const glm::vec3 foot = put_on_floor(line.back());
        const float breathe = 0.5f + 0.5f * static_cast<float>(std::sin(time * 3.0));
        out.push_back({foot, st.size * (2.6f + 0.6f * breathe), 0.55f + 0.25f * breathe});
        const float rise = static_cast<float>(std::fmod(time * 0.6, 1.0));
        for (int k = 0; k < 6; ++k) {
            const float h = (static_cast<float>(k) + rise) / 6.0f; // 0..1 up the column
            out.push_back({foot + glm::vec3(0.0f, 0.0f, 1.0f + h * 9.0f), st.size * (1.6f - 0.8f * h),
                           0.6f * (1.0f - h)});
        }
    }
    return out;
}

} // namespace hailstone::game
