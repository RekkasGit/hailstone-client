// render/shadow_chunks.h: the zone's solid casters merged into one chunk a cell for the shadow passes.
#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

#include "render/shadow_chunks.h"

using namespace nmsc::render;

namespace {

Box box(float x0, float x1) { return {glm::vec3(x0, 0.0f, 0.0f), glm::vec3(x1, 1.0f, 1.0f)}; }

} // namespace

TEST_CASE("merge_cell_chunks: one chunk a cell, in cell order, the draws' order within a cell") {
    // Two draws' indices, each sorted by cell: draw A cells 1, 5; draw B cells -3, 1, 5.
    std::vector<std::uint32_t> indices = {10, 11, 12, 13, 14, 15, // A, cell 1 (two triangles)
                                          20, 21, 22,             // A, cell 5
                                          30, 31, 32,             // B, cell -3
                                          40, 41, 42,             // B, cell 1
                                          50, 51, 52};            // B, cell 5
    const std::size_t original = indices.size();
    std::vector<CellChunk> chunks = {
        {1, 0, 6, box(0, 1)}, {5, 6, 3, box(4, 5)}, {-3, 9, 3, box(-3, -2)}, {1, 12, 3, box(1, 2)}, {5, 15, 3, box(5, 6)},
    };
    auto merged = merge_cell_chunks(chunks, indices);
    REQUIRE(merged.size() == 3);
    CHECK(merged[0].cell == -3);
    CHECK(merged[1].cell == 1);
    CHECK(merged[2].cell == 5);
    // The originals stay; the copies follow, cell by cell, end to end.
    REQUIRE(indices.size() == original * 2);
    CHECK(std::equal(indices.begin(), indices.begin() + static_cast<std::ptrdiff_t>(original),
                     std::vector<std::uint32_t>{10, 11, 12, 13, 14, 15, 20, 21, 22, 30, 31, 32, 40, 41, 42, 50, 51, 52}
                         .begin()));
    const std::vector<std::uint32_t> copies(indices.begin() + static_cast<std::ptrdiff_t>(original), indices.end());
    CHECK(copies == std::vector<std::uint32_t>{30, 31, 32, 10, 11, 12, 13, 14, 15, 40, 41, 42, 20, 21, 22, 50, 51, 52});
    CHECK(merged[0].first_index == original);
    CHECK(merged[0].index_count == 3);
    CHECK(merged[1].first_index == merged[0].first_index + 3);
    CHECK(merged[1].index_count == 9);
    CHECK(merged[2].first_index == merged[1].first_index + 9);
    CHECK(merged[2].index_count == 6);
    // A cell's box holds its chunks' boxes.
    CHECK(merged[1].box.min.x == 0.0f);
    CHECK(merged[1].box.max.x == 2.0f);
    CHECK(merged[2].box.min.x == 4.0f);
    CHECK(merged[2].box.max.x == 6.0f);
}

TEST_CASE("merge_cell_chunks: every triangle once, empty chunks skipped") {
    std::vector<std::uint32_t> indices = {1, 2, 3, 4, 5, 6};
    std::vector<CellChunk> chunks = {{7, 0, 0, box(0, 1)}, {7, 3, 3, box(0, 1)}, {2, 0, 3, box(0, 1)}};
    auto merged = merge_cell_chunks(chunks, indices);
    REQUIRE(merged.size() == 2);
    std::vector<std::uint32_t> copies(indices.begin() + 6, indices.end());
    std::vector<std::uint32_t> sorted = copies;
    std::sort(sorted.begin(), sorted.end());
    CHECK(sorted == std::vector<std::uint32_t>{1, 2, 3, 4, 5, 6});
    CHECK(merge_cell_chunks({}, indices).empty());
}
