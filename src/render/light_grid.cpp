#include "render/light_grid.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hailstone::render {

namespace {

// A light the grid can hold: a radius, and a finite reach (a NaN position or an infinite radius would make the
// grid's size, and the cell numbers cast from it, undefined). The zone's loader filters these too; this is the
// grid's own guard.
bool usable(const assets::PointLight& l) {
    if (!(l.radius > 0.0f) || !std::isfinite(l.radius)) return false;
    const glm::vec2 p(l.position);
    return std::isfinite(p.x - l.radius) && std::isfinite(p.x + l.radius) && std::isfinite(p.y - l.radius) &&
           std::isfinite(p.y + l.radius);
}

} // namespace

LightGrid build_light_grid(std::span<const assets::PointLight> lights, float cell, std::uint32_t max_per_cell) {
    LightGrid grid;
    if (!(cell > 0.0f) || !std::isfinite(cell)) return grid;
    glm::vec2 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    for (const auto& l : lights) {
        if (!usable(l)) continue;
        lo = glm::min(lo, glm::vec2(l.position) - l.radius);
        hi = glm::max(hi, glm::vec2(l.position) + l.radius);
    }
    if (lo.x > hi.x) return grid;
    glm::vec2 extent = hi - lo;
    if (!std::isfinite(extent.x) || !std::isfinite(extent.y)) return grid; // lights at both ends of the float range
    // Coarser cells when the lights spread too far for the cell count.
    // In double: a spread near the float range squared would be infinite.
    const double area_cells = (static_cast<double>(extent.x) / cell + 1.0) * (static_cast<double>(extent.y) / cell + 1.0);
    if (area_cells > static_cast<double>(kMaxGridCells))
        cell = static_cast<float>(cell * std::sqrt(area_cells / static_cast<double>(kMaxGridCells)) * 1.01);
    grid.origin = lo;
    grid.cell = cell;
    // Within kMaxGridCells whatever the rounding (the cell's growth above aims just under it).
    auto cells_across = [&](float span) {
        const float n = std::floor(span / cell) + 1.0f;
        return n >= static_cast<float>(kMaxGridCells) ? kMaxGridCells : std::max(1u, static_cast<std::uint32_t>(n));
    };
    grid.width = cells_across(extent.x);
    grid.height = std::min(cells_across(extent.y), std::max(1u, kMaxGridCells / grid.width));

    std::vector<std::vector<std::uint32_t>> lists(std::size_t(grid.width) * grid.height);
    for (std::uint32_t i = 0; i < lights.size(); ++i) {
        const auto& l = lights[i];
        if (!usable(l)) continue;
        glm::vec2 p(l.position);
        // A cell number, clamped before the cast: past the grid's last cell is that cell.
        auto cell_of = [&](float d, std::uint32_t last) {
            const float n = std::floor(d / cell);
            return n <= 0.0f ? 0u : n >= static_cast<float>(last) ? last : static_cast<std::uint32_t>(n);
        };
        const std::uint32_t x0 = cell_of(p.x - l.radius - lo.x, grid.width - 1);
        const std::uint32_t y0 = cell_of(p.y - l.radius - lo.y, grid.height - 1);
        const std::uint32_t x1 = cell_of(p.x + l.radius - lo.x, grid.width - 1);
        const std::uint32_t y1 = cell_of(p.y + l.radius - lo.y, grid.height - 1);
        for (std::uint32_t y = y0; y <= y1; ++y)
            for (std::uint32_t x = x0; x <= x1; ++x) {
                // The circle against the cell's square: the nearest point of the square within the radius.
                glm::vec2 c0 = lo + glm::vec2(x, y) * cell;
                glm::vec2 nearest = glm::clamp(p, c0, c0 + cell);
                if (glm::dot(nearest - p, nearest - p) <= l.radius * l.radius)
                    lists[std::size_t(y) * grid.width + x].push_back(i);
            }
    }
    grid.cells.resize(lists.size());
    for (std::size_t c = 0; c < lists.size(); ++c) {
        auto& list = lists[c];
        if (list.size() > max_per_cell) {
            glm::vec2 centre = lo + (glm::vec2(c % grid.width, c / grid.width) + 0.5f) * cell;
            auto d2 = [&](std::uint32_t i) {
                glm::vec2 d = glm::vec2(lights[i].position) - centre;
                return glm::dot(d, d);
            };
            std::partial_sort(list.begin(), list.begin() + max_per_cell, list.end(),
                              [&](std::uint32_t a, std::uint32_t b) { return d2(a) < d2(b); });
            list.resize(max_per_cell);
        }
        grid.cells[c] = glm::uvec2(static_cast<std::uint32_t>(grid.indices.size()), static_cast<std::uint32_t>(list.size()));
        grid.indices.insert(grid.indices.end(), list.begin(), list.end());
    }
    return grid;
}

} // namespace hailstone::render
