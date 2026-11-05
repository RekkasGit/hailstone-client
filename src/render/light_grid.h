#pragma once

// The zone's static point lights (lights.wld) for the modern lighting's per-pixel pass (shaders/zone.frag): a
// uniform grid over the world's XY plane, each cell listing the lights whose reach overlaps it, so a pixel looks at
// its cell's lights only.

#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "assets/model.h"

namespace nmsc::render {

struct LightGrid {
    glm::vec2 origin{0.0f};       // the world XY of cell (0, 0)'s corner
    float cell = 64.0f;           // a cell's side in world units
    std::uint32_t width = 0, height = 0;
    std::vector<glm::uvec2> cells; // per cell, row by row: first index, count (into `indices`)
    std::vector<std::uint32_t> indices; // light numbers into the list given
};

inline constexpr std::uint32_t kMaxGridCells = 512 * 512;
inline constexpr std::uint32_t kMaxLightsPerCell = 24;

// Lights without a radius are left out. A cell keeps at most `max_per_cell` lights, the nearest to its centre; the
// cell size grows past `cell` when the lights spread over more than kMaxGridCells cells.
LightGrid build_light_grid(std::span<const assets::PointLight> lights, float cell = 64.0f,
                           std::uint32_t max_per_cell = kMaxLightsPerCell);

} // namespace nmsc::render
