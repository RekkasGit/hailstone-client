#include "game/path_trail.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace hailstone::game {

namespace {

const std::array<glm::vec3, 7> kRainbow{{
    {1.0f, 0.04f, 0.02f}, {1.0f, 0.35f, 0.02f}, {1.0f, 0.90f, 0.02f}, {0.04f, 1.0f, 0.12f},
    {0.02f, 0.80f, 1.0f}, {0.12f, 0.08f, 1.0f}, {0.80f, 0.04f, 1.0f}
}};

float smooth(float edge0, float edge1, float x) {
    if (edge1 <= edge0) return x >= edge1 ? 1.0f : 0.0f;
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

bool finite(const glm::vec3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

glm::vec2 direction(const glm::vec3& a, const glm::vec3& b) {
    const glm::vec2 d = glm::vec2(b - a);
    const float len = glm::length(d);
    return len > 0.0001f ? d / len : glm::vec2(0.0f);
}

glm::vec2 corner_direction(std::span<const glm::vec3> line, std::size_t i, const glm::vec2& fallback) {
    const glm::vec2 before = i > 0 ? direction(line[i - 1], line[i]) : fallback;
    const glm::vec2 after = i + 1 < line.size() ? direction(line[i], line[i + 1]) : fallback;
    const glm::vec2 sum = before + after;
    return glm::length(sum) > 0.0001f ? glm::normalize(sum) : fallback;
}

} // namespace

std::vector<TrailSprite> build_trail(std::span<const glm::vec3> line, const TrailStyle& st, double time,
                                     const TrailFloorFn& floor, std::size_t max_sprites) {
    std::vector<TrailSprite> out;
    if (line.size() < 2 || max_sprites == 0 || !floor || !std::isfinite(time)) return out;
    for (const auto& p : line) if (!finite(p)) return out;
    if (!std::isfinite(st.spacing) || !std::isfinite(st.far_end) || !std::isfinite(st.near_start)) return out;
    const float spacing = std::max(st.spacing, 0.25f);
    const float band_len = std::max(st.band_length, 0.5f);
    const double cycle = static_cast<double>(band_len) / std::max(static_cast<double>(st.band_speed), 0.001);
    const float shift = static_cast<float>(std::fmod(time, cycle) / cycle) * band_len;
    const float sway_time = static_cast<float>(std::fmod(time, 6.283185307 / 0.9)) * 0.9f;
    const std::size_t strands = st.rainbow ? kRainbow.size() : 1;

    // Follow the last support, not the straight chord: a long hill or valley may be arbitrarily far from that
    // chord. A missing deck must not pull the trail down to the ground below, nor a tunnel up onto its roof.
    // Only route vertices can restart it after a break.
    auto support = [&](const glm::vec3& p, float reference, float up, float down) -> std::optional<float> {
        auto z = floor({p.x, p.y, reference + up});
        if (!z || !std::isfinite(*z) || *z > reference + up || *z < reference - down) return std::nullopt;
        return z;
    };
    auto anchor = [&](const glm::vec3& p) -> std::optional<float> {
        // Feet or a mesh corner may sit a little inside the visible ground. Keep the first nearby floor found:
        // starting the cast high could select a deck above a perfectly good lower floor.
        for (float up = 0.5f; up <= 3.0f; up += 0.5f)
            if (const auto z = support(p, p.z, up, 6.0f)) return z;
        return std::nullopt;
    };
    std::optional<float> height = anchor(line.front());
    float along = 0.0f;
    float next = std::max(st.near_start, 0.0f);
    // Bound work even when every sample is hidden, or the supplied line has enormous coordinates.
    const std::size_t max_steps = std::min(max_sprites, std::size_t{16384}) * 4;
    std::size_t steps = 0;
    for (std::size_t i = 0; i + 1 < line.size() && out.size() < max_sprites && steps < max_steps; ++i) {
        const glm::vec3 a = line[i], b = line[i + 1];
        const float len = glm::length(b - a);
        if (!std::isfinite(len)) break;
        if (len <= 0.0001f) continue;
        const glm::vec2 forward = direction(a, b);
        // A vertical link is a climb/drop/swim, not a stretch of supported ground.
        if (glm::dot(forward, forward) == 0.0f) {
            along += len;
            height.reset();
            if (next < along) next += std::ceil((along - next) / spacing) * spacing;
            continue;
        }
        if (!height) height = anchor(a);
        const glm::vec2 start_dir = corner_direction(line, i, forward);
        const glm::vec2 end_dir = corner_direction(line, i + 1, forward);
        const float bend = std::min(len * 0.5f, 2.0f);
        float local = 0.0f;
        while (local <= len && along + local <= st.far_end && out.size() < max_sprites && steps < max_steps) {
            const float distance = along + local;
            const glm::vec3 p = a + (b - a) * (local / len);
            if (height) height = support(p, *height, 3.0f, 3.0f);
            ++steps;
            if (distance + 0.0001f >= next) {
                if (height) {
                    glm::vec2 tangent = forward;
                    if (local < bend) tangent = glm::normalize(glm::mix(start_dir, forward, local / bend));
                    else if (len - local < bend) tangent = glm::normalize(glm::mix(end_dir, forward, (len - local) / bend));
                    const glm::vec2 side(-tangent.y, tangent.x);
                    const float phase = (next - shift) / band_len;
                    const float wave = std::pow(0.5f + 0.5f * std::cos(phase * 6.2831853f), 6.0f);
                    const float fade = smooth(st.near_start, st.near_end, next) *
                                       (1.0f - smooth(st.far_start, st.far_end, next));
                    const float alpha = std::clamp(st.alpha * (1.0f + st.band_boost * wave) * fade, 0.0f, 1.0f);
                    if (alpha > 0.01f)
                        for (std::size_t k = 0; k < strands && out.size() < max_sprites; ++k) {
                            const float index = static_cast<float>(k);
                            const float offset = st.rainbow ? (index - 3.0f) * st.strand_spacing +
                                st.sway * std::sin(distance * 0.19f - sway_time + index * 0.45f) : 0.0f;
                            glm::vec3 q(p.x + side.x * offset, p.y + side.y * offset, *height);
                            auto z = offset == 0.0f ? height : support(q, *height, 3.0f, 3.0f);
                            if (!z) continue;
                            q.z = *z + st.lift;
                            out.push_back({q, st.size * (1.0f + 0.5f * wave), alpha, st.rainbow ? kRainbow[k] : st.color, next});
                        }
                }
                next += spacing;
            }
            if (local >= len || next > st.far_end) break;
            const float advance = std::min({1.0f, len - local, std::max(next - distance, 0.0001f)});
            if (!(local + advance > local)) break;
            local += advance;
        }
        along += len;
        if (next > st.far_end) break;
    }
    if (st.beacon && out.size() < max_sprites) {
        if (const auto z = anchor(line.back())) {
            const glm::vec3 foot(line.back().x, line.back().y, *z + st.lift);
            const float breathe = 0.5f + 0.5f * static_cast<float>(std::sin(time * 3.0));
            out.push_back({foot, st.size * (2.6f + 0.6f * breathe), 0.55f + 0.25f * breathe,
                           st.rainbow ? kRainbow[0] : st.color});
            const float rise = static_cast<float>(std::fmod(time * 0.6, 1.0));
            for (std::size_t k = 0; k < 6 && out.size() < max_sprites; ++k) {
                const float h = (static_cast<float>(k) + rise) / 6.0f;
                out.push_back({foot + glm::vec3(0.0f, 0.0f, 1.0f + h * 9.0f), st.size * (1.6f - 0.8f * h),
                               0.6f * (1.0f - h), st.rainbow ? kRainbow[k + 1] : st.color});
            }
        }
    }
    return out;
}

std::vector<TrailRibbonPiece> build_navigation_trail(std::span<const glm::vec3> line, const TrailStyle& style, double time,
                                                   const TrailFloorFn& floor, std::size_t max_pieces) {
    std::vector<TrailRibbonPiece> out;
    if (max_pieces < 2 || !floor) return out;
    TrailStyle st = style;
    st.rainbow = false;
    st.beacon = false;
    st.band_boost = 0.0f;
    st.alpha = 0.95f;
    const auto samples = build_trail(line, st, time, floor, max_pieces / 2 + 1);
    const float spacing = std::max(st.spacing, 0.25f);
    constexpr float border_width = 0.85f, gold_width = 0.5f;
    auto ground = [&](glm::vec3 p) -> std::optional<float> {
        const float reference = p.z - st.lift;
        p.z = reference + 3.0f;
        auto z = floor(p);
        if (!z || !std::isfinite(*z) || *z < reference - 3.0f || *z > reference + 3.0f) return std::nullopt;
        return z;
    };
    for (std::size_t i = 1; i < samples.size() && out.size() + 2 <= max_pieces; ++i) {
        const auto& a = samples[i - 1];
        const auto& b = samples[i];
        // A restart at a later route vertex must not join across the hidden, unsupported samples.
        if (b.distance - a.distance > spacing + 0.001f) continue;
        const glm::vec3 delta = b.position - a.position;
        const float len = glm::length(delta);
        const glm::vec2 xy(delta);
        const float flat_len = glm::length(xy);
        if (!(flat_len > 0.0001f) || !(len > 0.0001f)) continue;
        const glm::vec3 side(-xy.y / flat_len, xy.x / flat_len, 0.0f);
        const auto al = ground(a.position - side * (border_width * 0.5f));
        const auto ar = ground(a.position + side * (border_width * 0.5f));
        const auto bl = ground(b.position - side * (border_width * 0.5f));
        const auto br = ground(b.position + side * (border_width * 0.5f));
        if (!al || !ar || !bl || !br) continue;
        const glm::vec3 forward = delta / len;
        glm::vec3 across = side;
        across.z = ((*ar - *al) + (*br - *bl)) / (2.0f * border_width);
        const glm::vec3 normal = glm::cross(forward, across);
        if (!(glm::length(normal) > 0.0001f)) continue;
        across = glm::normalize(glm::cross(normal, forward));
        const glm::vec3 center = (a.position + b.position) * 0.5f;
        // A stair's endpoints can both be supported while their connecting plane crosses air or the riser. The
        // centre and corners must agree with their ground, not merely have some ground within the search range.
        auto on_plane = [&](const glm::vec3& p) {
            const auto z = ground(p);
            return z && std::abs(*z - (p.z - st.lift)) <= 0.35f;
        };
        bool supported = on_plane(center);
        for (const float end : {-0.5f, 0.5f})
            for (const float edge : {-0.5f, 0.5f})
                if (!on_plane(center + forward * (len * end) + across * (border_width * edge))) supported = false;
        if (!supported) continue;
        const float alpha = std::min(a.alpha, b.alpha);
        out.push_back({center, across, forward, border_width, len, {0.025f, 0.025f, 0.03f}, alpha});
        out.push_back({center + glm::vec3(0, 0, 0.02f), across, forward, gold_width, len, {1.0f, 0.82f, 0.08f}, alpha});
    }
    return out;
}

std::vector<TrailRibbonPiece> build_rainbow_strands(std::span<const glm::vec3> line, const TrailStyle& style, double time,
                                                 const TrailFloorFn& floor, std::size_t max_pieces) {
    std::vector<TrailRibbonPiece> out;
    if (max_pieces == 0 || !floor || !std::isfinite(time)) return out;
    constexpr std::size_t strand_count = 14;
    constexpr float width = 0.06f, hover = 3.0f, winding = 0.8f, curl = 0.7f;
    constexpr double tau = 6.283185307179586;
    TrailStyle st = style;
    st.rainbow = false;
    st.beacon = false;
    st.lift = 0.0f;
    st.spacing = std::min(style.spacing, 0.4f);
    st.alpha = 0.85f;
    st.band_boost = 0.15f;
    const float spacing = std::max(st.spacing, 0.25f);
    const auto samples = build_trail(line, st, time, floor,
                                   std::min(max_pieces / strand_count + 2, std::size_t{16384}));
    if (samples.size() < 2) return out;
    out.reserve(std::min(max_pieces, samples.size() * strand_count));
    struct Motion {
        float phase, frequency, shift, shift2, shift3;
    };
    std::array<Motion, strand_count> motion;
    for (std::size_t k = 0; k < strand_count; ++k) {
        const float color = static_cast<float>(k % kRainbow.size()), copy = static_cast<float>(k / kRainbow.size());
        const double speed = 0.65 + 0.07 * static_cast<double>(color) + 0.11 * static_cast<double>(copy);
        auto shift = [&](double rate) { return static_cast<float>(std::fmod(time, tau / rate) * rate); };
        motion[k] = {static_cast<float>(k) * 2.39996323f, 0.28f + 0.017f * color + 0.025f * copy,
                     shift(speed), shift(speed * 0.63), shift(speed * 0.87)};
    }
    struct Point {
        glm::vec3 position;
        float ground, distance, alpha;
    };
    std::array<std::optional<Point>, strand_count> previous;
    auto adjacent = [&](std::size_t a, std::size_t b) {
        const float d = samples[b].distance - samples[a].distance;
        return d > 0.0f && d <= spacing + 0.001f;
    };
    auto support = [&](glm::vec3 p, float reference) -> std::optional<float> {
        p.z = reference + 3.0f;
        const auto z = floor(p);
        if (!z || !std::isfinite(*z) || *z > reference + 3.0f || *z < reference - 3.0f) return std::nullopt;
        return z;
    };
    for (std::size_t i = 0; i < samples.size() && out.size() < max_pieces; ++i) {
        const auto& sample = samples[i];
        glm::vec2 tangent(0.0f);
        if (i > 0 && adjacent(i - 1, i)) tangent += direction(samples[i - 1].position, sample.position);
        const glm::vec2 next = i + 1 < samples.size() && adjacent(i, i + 1) ?
                              direction(sample.position, samples[i + 1].position) : glm::vec2(0.0f);
        tangent += next;
        if (glm::length(tangent) <= 0.0001f) tangent = glm::length(next) > 0.0f ? next : glm::vec2(1.0f, 0.0f);
        else tangent = glm::normalize(tangent);
        const glm::vec3 side(-tangent.y, tangent.x, 0.0f);
        for (std::size_t k = 0; k < strand_count && out.size() < max_pieces; ++k) {
            const auto& m = motion[k];
            const float phase = sample.distance * m.frequency + m.phase - m.shift;
            const float lateral = winding * (0.78f * std::sin(phase) +
                                            0.22f * std::sin(sample.distance * m.frequency * 0.53f + m.phase * 1.7f - m.shift2));
            const float vertical = curl * (0.78f * std::cos(phase + 0.9f) +
                                          0.22f * std::sin(sample.distance * m.frequency * 0.83f + m.phase * 0.7f - m.shift3));
            glm::vec3 position = sample.position + side * lateral;
            const auto ground = support(position, sample.position.z);
            if (!ground) {
                previous[k].reset();
                continue;
            }
            position.z = *ground + hover + vertical;
            Point point{position, *ground, sample.distance, sample.alpha};
            if (previous[k] && point.distance - previous[k]->distance <= spacing + 0.001f) {
                const auto& before = *previous[k];
                const glm::vec3 delta = point.position - before.position;
                const float len = glm::length(delta);
                const glm::vec3 center = (point.position + before.position) * 0.5f;
                const float ground_mean = (point.ground + before.ground) * 0.5f;
                // Endpoint support alone could bridge a narrow hole or cut diagonally across a stair riser.
                const auto middle_ground = support(center, ground_mean);
                if (middle_ground && std::abs(*middle_ground - ground_mean) <= 0.35f && len > 0.0001f) {
                    const glm::vec3 forward = delta / len;
                    glm::vec3 across = glm::cross(glm::vec3(0, 0, 1), forward);
                    if (glm::length(across) < 0.0001f) across = glm::cross(glm::vec3(1, 0, 0), forward);
                    across = glm::normalize(across);
                    out.push_back({center, across, forward, width, len, kRainbow[k % kRainbow.size()],
                                   std::min(before.alpha, point.alpha)});
                }
            }
            previous[k] = point;
        }
    }
    return out;
}

} // namespace hailstone::game
