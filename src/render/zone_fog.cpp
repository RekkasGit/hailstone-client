#include "render/zone_fog.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "render/horizon.h"

namespace hailstone::render {

namespace {

// Channel `shift` of the field bilinearly at `p` (0..1), clamped at the grid's edge as the sampler is.
float sample(const ZoneFogField& f, glm::vec2 p, int shift) {
    const glm::vec2 u = (p - f.lo) / f.cell - 0.5f;
    const float fx = std::floor(u.x), fy = std::floor(u.y);
    const float tx = u.x - fx, ty = u.y - fy;
    auto at = [&](float x, float y) {
        const int ix = std::clamp(static_cast<int>(x), 0, f.nx - 1), iy = std::clamp(static_cast<int>(y), 0, f.ny - 1);
        const std::uint32_t t =
            f.texels[static_cast<std::size_t>(iy) * static_cast<std::size_t>(f.nx) + static_cast<std::size_t>(ix)];
        return static_cast<float>((t >> shift) & 0xffu) / 255.0f;
    };
    const float a = at(fx, fy) + (at(fx + 1.0f, fy) - at(fx, fy)) * tx;
    const float b = at(fx, fy + 1.0f) + (at(fx + 1.0f, fy + 1.0f) - at(fx, fy + 1.0f)) * tx;
    return a + (b - a) * ty;
}

} // namespace

float ZoneFogField::distance_at(glm::vec2 p) const {
    if (!valid()) return 0.0f;
    const glm::vec2 hi = lo + glm::vec2(static_cast<float>(nx), static_cast<float>(ny)) * cell;
    const float extra = glm::length(glm::max(lo - p, glm::vec2(0.0f)) + glm::max(p - hi, glm::vec2(0.0f)));
    return sample(*this, p, 0) * 255.0f * kZoneFogDistanceStep + extra;
}

float ZoneFogField::floor_at(glm::vec2 p) const {
    if (!valid()) return 0.0f;
    return floor_lo + sample(*this, p, 8) * floor_range;
}

ZoneFogField zone_fog_field(const HorizonField& field) {
    ZoneFogField out;
    if (!field.valid() || field.dist.size() != static_cast<std::size_t>(field.nx) * static_cast<std::size_t>(field.ny))
        return out;
    const std::size_t cells = field.dist.size();
    // The floor over the open squares (past the zone; a pocket keeps the fog out, so its floor does not count).
    float lo = std::numeric_limits<float>::infinity(), hi = -lo;
    for (std::size_t i = 0; i < cells; ++i)
        if (field.open[i] && std::isfinite(field.edge[i])) {
            lo = std::min(lo, field.edge[i]);
            hi = std::max(hi, field.edge[i]);
        }
    if (!(lo <= hi)) return out; // no open square: the zone's terrain fills its grid
    out.lo = field.lo;
    out.cell = field.cell;
    out.nx = field.nx;
    out.ny = field.ny;
    out.floor_lo = lo;
    out.floor_range = std::max(hi - lo, 1.0f);
    out.texels.resize(cells);
    for (std::size_t i = 0; i < cells; ++i) {
        float d = 0.0f, h = lo;
        if (field.open[i]) {
            d = field.dist[i] * field.cell;
            if (std::isfinite(field.edge[i])) h = field.edge[i];
        } else if (field.covered(i)) {
            // The fog's floor under the zone's own squares is their highest terrain (the edge as the open squares
            // beside them carry it out), so it does not dip where it fades in.
            h = std::isfinite(field.zmax[i]) ? field.zmax[i] : lo;
        }
        const auto r = static_cast<std::uint32_t>(std::lround(std::clamp(d / kZoneFogDistanceStep, 0.0f, 255.0f)));
        const auto g = static_cast<std::uint32_t>(std::lround(std::clamp((h - lo) / out.floor_range, 0.0f, 1.0f) * 255.0f));
        out.texels[i] = r | (g << 8) | 0xff000000u;
    }
    return out;
}

float zone_fog_density(const ZoneFogField& field, glm::vec3 p) {
    if (!field.valid()) return 0.0f;
    const float d = std::clamp(field.distance_at(glm::vec2(p)) / kZoneFogRamp, 0.0f, 1.0f);
    const float ramp = d * d * (3.0f - 2.0f * d);
    const float over = p.z - field.floor_at(glm::vec2(p));
    const float top = std::clamp((over - (kZoneFogTop - kZoneFogSoft)) / (2.0f * kZoneFogSoft), 0.0f, 1.0f);
    return kZoneFogDensity * ramp * (1.0f - top * top * (3.0f - 2.0f * top));
}

float zone_fog_transmittance(const ZoneFogField& field, glm::vec3 from, glm::vec3 to) {
    const float length = glm::distance(from, to);
    if (!field.valid() || length <= 0.0f) return 1.0f;
    const glm::vec3 dir = (to - from) / length;
    // Steps closer together near the camera (t = length x s^2), each sampled at its middle.
    float tau = 0.0f;
    for (int i = 0; i < kZoneFogSteps; ++i) {
        const float n = static_cast<float>(kZoneFogSteps);
        const float s0 = static_cast<float>(i) / n, s1 = static_cast<float>(i + 1) / n;
        const float t0 = length * s0 * s0, t1 = length * s1 * s1;
        const float sm = (s0 + s1) * 0.5f;
        tau += zone_fog_density(field, from + dir * (length * sm * sm)) * (t1 - t0);
    }
    return std::exp(-tau);
}

} // namespace hailstone::render
