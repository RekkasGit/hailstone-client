#include "render/light_grid.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hailstone::render {

LightGrid build_light_grid(std::span<const assets::PointLight> lights, float cell, std::uint32_t max_per_cell) {
    LightGrid grid;
    glm::vec2 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    for (const auto& l : lights) {
        if (!(l.radius > 0.0f)) continue;
        lo = glm::min(lo, glm::vec2(l.position) - l.radius);
        hi = glm::max(hi, glm::vec2(l.position) + l.radius);
    }
    if (lo.x > hi.x) return grid;
    glm::vec2 extent = hi - lo;
    // Coarser cells when the lights spread too far for the cell count.
    float area_cells = (extent.x / cell + 1.0f) * (extent.y / cell + 1.0f);
    if (area_cells > static_cast<float>(kMaxGridCells)) cell *= std::sqrt(area_cells / static_cast<float>(kMaxGridCells)) * 1.01f;
    grid.origin = lo;
    grid.cell = cell;
    grid.width = static_cast<std::uint32_t>(std::floor(extent.x / cell)) + 1;
    grid.height = static_cast<std::uint32_t>(std::floor(extent.y / cell)) + 1;

    std::vector<std::vector<std::uint32_t>> lists(std::size_t(grid.width) * grid.height);
    for (std::uint32_t i = 0; i < lights.size(); ++i) {
        const auto& l = lights[i];
        if (!(l.radius > 0.0f)) continue;
        glm::vec2 p(l.position);
        auto x0 = static_cast<std::uint32_t>(std::max(0.0f, std::floor((p.x - l.radius - lo.x) / cell)));
        auto y0 = static_cast<std::uint32_t>(std::max(0.0f, std::floor((p.y - l.radius - lo.y) / cell)));
        auto x1 = std::min(grid.width - 1, static_cast<std::uint32_t>(std::floor((p.x + l.radius - lo.x) / cell)));
        auto y1 = std::min(grid.height - 1, static_cast<std::uint32_t>(std::floor((p.y + l.radius - lo.y) / cell)));
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
