#include "render/shadow_chunks.h"

#include <algorithm>

namespace nmsc::render {

std::vector<CellChunk> merge_cell_chunks(std::span<const CellChunk> chunks, std::vector<std::uint32_t>& indices) {
    std::vector<std::uint32_t> order(chunks.size());
    for (std::uint32_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](std::uint32_t a, std::uint32_t b) { return chunks[a].cell < chunks[b].cell; });
    std::size_t total = 0;
    for (const auto& c : chunks) total += c.index_count;
    indices.reserve(indices.size() + total);
    std::vector<CellChunk> out;
    for (std::uint32_t i : order) {
        const CellChunk& c = chunks[i];
        if (c.index_count == 0) continue;
        if (out.empty() || out.back().cell != c.cell) {
            CellChunk m;
            m.cell = c.cell;
            m.first_index = static_cast<std::uint32_t>(indices.size());
            m.box = c.box;
            out.push_back(m);
        }
        CellChunk& m = out.back();
        // By position, not by iterator: the copies go to the same vector.
        for (std::uint32_t k = 0; k < c.index_count; ++k) indices.push_back(indices[c.first_index + k]);
        m.index_count += c.index_count;
        m.sways |= c.sways;
        m.box.min = glm::min(m.box.min, c.box.min);
        m.box.max = glm::max(m.box.max, c.box.max);
    }
    return out;
}

} // namespace nmsc::render
