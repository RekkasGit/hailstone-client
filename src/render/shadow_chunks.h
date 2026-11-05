#pragma once

// The zone's solid casters in one draw a cell (docs/graphics.md, "Culling"): the shadow passes need no texture for a
// material that is not alpha-tested, so the opaque draws' chunks of one 128-unit cell, one per material, are merged
// into one chunk whose indices are copied, cell by cell, to the end of the zone's index buffer. A cascade then costs
// about one draw call per column of cells in view instead of one per material per column.

#include <cstdint>
#include <span>
#include <vector>

#include "render/culling.h"

namespace nmsc::render {

// A run of indices, all in cell `cell` (ZoneRenderer's cell key, ordered x first), with its box.
struct CellChunk {
    std::int64_t cell = 0;
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    Box box;
    bool sways = false; // some of its vertices sway in the wind
};

// Appends the indices of `chunks` to `indices` (which they point into) cell by cell, in key order and, within a cell,
// in the chunks' order; returns one chunk a cell, pointing at the copies, its box round those of its chunks, swaying
// when one of them does.
std::vector<CellChunk> merge_cell_chunks(std::span<const CellChunk> chunks, std::vector<std::uint32_t>& indices);

} // namespace nmsc::render
