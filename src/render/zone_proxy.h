#pragma once

// Distant copies of the zones round the current one (Hailstone's own; docs/graphics.md "Distant copies"): a zone the
// world atlas places past the current one, baked once from its own files on the CPU into a low-detail copy (its
// surface seen from above, terrain and buildings, as heights on a grid; its colour seen from above as one picture, with
// the colour of its walls beside it; its trees as pictures on crossed cards where they stand), cached on disk, and
// drawn far out past the current zone's edge in place of the land there (app/distant_copies.h). The original drew
// nothing past a zone's own geometry.

#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "assets/model.h"
#include "assets/texture.h"
#include "assets/zone.h"

namespace hailstone::render {

struct HorizonField;

// The bake: heights every kProxyCell units, colour every kProxyTexel (coarser for a big zone: at most kProxyMaxTexture
// texels a side).
inline constexpr float kProxyCell = 16.0f;
inline constexpr float kProxyTexel = 8.0f;
inline constexpr std::uint32_t kProxyMaxTexture = 1024;
// Its boundary: the cells within kProxyEdgeRing of the edge of what it has stand where a zone keeps its boundary walls
// and filler. Such a cell is kept only when it stands at most kProxyWallRise over the zone's own ground near it (a
// city's wall stays; the mountains ringing a wild zone go). A piece of the copy smaller than kProxyMinIsland cells,
// cut off from the rest, goes too (a stray post, a bit of wall left by the ring).
inline constexpr float kProxyEdgeRing = 384.0f;
inline constexpr float kProxyWallRise = 150.0f;
inline constexpr std::size_t kProxyMinIsland = 256;

struct ZoneProxy {
    std::string zone;
    std::uint64_t key = 0; // the zone's files it was baked from (app::atlas_zone_key)
    // Heights over a grid in its own coordinates, at the middle of each cell; NaN where it has nothing (and over its
    // water, which the land draws).
    glm::vec2 lo{0.0f};
    float cell = kProxyCell;
    int nx = 0, ny = 0;
    std::vector<float> height;
    std::vector<std::uint8_t> drawn; // 1 where the cell is drawn (it has a height, and its boundary test passed)
    // The picture (RGBA8): its colour seen from above, `top_width` x `top_height` texels of `texel` units from `lo`
    // (row 0 at lo's y), then kProxyStripGap rows, then the walls' colour, a texel a cell (nx x ny, from column 0).
    assets::Image picture;
    std::uint32_t top_width = 0, top_height = 0;
    float texel = kProxyTexel;
    // The zone's baked light on its terrain (the median vertex colour, RGBA8 as assets::Vertex::color): every vertex of
    // the copy takes it.
    std::uint32_t light = 0xff808080u;
    struct TreeKind {
        glm::vec3 lo{0.0f}, hi{0.0f}; // render::TreeImpostor's box
        assets::Image image;           // its picture (render::TreeImpostor::image)
    };
    std::vector<TreeKind> tree_kinds;
    struct Tree {
        glm::mat4 transform{1.0f}; // in its own coordinates
        std::uint32_t kind = 0;
        std::uint32_t tint = 0xffffffffu; // the placement's light, RGBA8
    };
    std::vector<Tree> trees;
    bool valid() const {
        const auto n = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);
        return nx > 1 && ny > 1 && height.size() == n && drawn.size() == n && !picture.mips.empty() &&
               picture.width >= static_cast<std::uint32_t>(nx) && top_width > 0 && top_height > 0;
    }
};
inline constexpr std::uint32_t kProxyStripGap = 4;

// Bakes `zone`'s copy (`key`: its files, kept with it). Its water and lava, what is not drawn or glows, and its plants
// are left out of its surface; its trees (plants at least kLandTreeMinHeight tall) are kept as tree pictures. Not valid
// when it has nothing to draw, or `cancelled` was set.
ZoneProxy bake_zone_proxy(const assets::Zone& zone, std::uint64_t key, const std::atomic<bool>* cancelled = nullptr);

// The cache (app/distant_copies.h; cache/proxies/<zone>.bin).
std::vector<std::byte> write_zone_proxy(const ZoneProxy& proxy);
// nullopt when the bytes are not a copy of this version, or are damaged.
std::optional<ZoneProxy> read_zone_proxy(std::span<const std::byte> data);
// Its zone and key from the start of a cache file (nullopt: not a copy of this version).
std::optional<std::pair<std::string, std::uint64_t>> read_zone_proxy_header(std::span<const std::byte> data);

// Where a copy is drawn: from kProxyNear past the current zone's edge (the land's field: its squares, how far each lies
// from the zone), rising over kProxyFade out of the land (kProxySink under its own height at kProxyNear, kProxyRaise
// over it past the band, so it draws over the land where the two meet); and nowhere in the zone's own squares or its
// pockets. Coarser with the distance: a cell every proxy_step(d) cells, kProxyChunk cells to a chunk, the chunks of a
// coarser step skirted down so no crack shows between them.
inline constexpr float kProxyNear = 1200.0f;
inline constexpr float kProxyFade = 400.0f;
inline constexpr float kProxySink = 60.0f;
inline constexpr float kProxyRaise = 3.0f;
inline constexpr int kProxyChunk = 32;
// 1 within 3000 units of the zone's edge, 2 within 6000, 4 past.
int proxy_step(float distance);
// How far past the current zone's edge `p` lies (its coordinates), by the land's field; a negative number in the zone's
// own squares and its pockets. Without a field (none built), far.
float proxy_distance(const HorizonField* field, glm::vec2 p);

struct ProxyZone {
    // A zone of its own for render::ZoneRenderer (in the copy's coordinates: draw it moved by the offset): one terrain
    // mesh a chunk, the trees as placed objects, its pictures in generated_files.
    assets::Zone zone;
    // The land's squares (HorizonField::mesh_square) it covers whole past its band, for the current zone's
    // set_distant_hidden.
    std::vector<std::int64_t> footprint;
    std::size_t triangles = 0, trees = 0;
};
// `offset`: its coordinates + offset are the current zone's; `keep`: the planes it is kept by (in the current zone's
// coordinates, game::atlas_cuts; xyz . p + w >= 0); `field`: the current zone's land (null: drawn whole).
ProxyZone proxy_zone(const ZoneProxy& proxy, const HorizonField* field, glm::vec3 offset,
                     std::span<const glm::vec4> keep);

} // namespace hailstone::render
