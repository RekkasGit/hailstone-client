#pragma once

// Classic WLD detail textures: the names a BMINFO (0x03) holds after its diffuse.
//
// The graphics DLL's BMINFO loader (0x1001b8c0 in the shipped EQGraphicsDX9.dll) types each name:
//   - two names, the second with "_LAYER": a Luclin character layer (assets/character_dress.cpp);
//   - two names, the second "<file>_DETAIL_<scale>" (CLIFFROCK02.DDS_DETAIL_4.000000): one detail texture over the
//     whole surface, its UVs x scale (atof);
//   - three to sixteen names: name 1 is a palette map (TRANQBASE2APAL.BMP), names 2.. are "<n>, <scale>, <value>,
//     <file>" entries (1, 3, 2, TRANQSANDDET01.DDS), entry i drawn with UVs x 18 x scale (atoi) where the map's pixel
//     under a vertex is i. Seventeen or more names keep only the diffuse.
// CMaterial::ResolveTransparency @0x100138d0 makes the first a material of type 1 (its +0x40 detail, +0x44 scale)
// and the second type 2 (a palette registered with the scene graph). The zone-region builder 0x10019780 then adds a
// pass per detail over the region's triangles (CRender::BuildRegionVertices @0x1008d380 with mode 2 or 3), drawn by
// SPL/RegionOldDetailSingle or SPL/RegionOldDetailPalette: SRCALPHA/INVSRCALPHA blend (state flag +5), no z-write,
// texture x the lit vertex colour, alpha = the vertex alpha (TEXTUREFACTOR 0xff x diffuse; the texture's alpha is
// not read). A palette pass sets each vertex's alpha to 0xff where the map gives the vertex that entry and 0 where it
// does not, and takes the triangles that have at least one such vertex. docs/formats/wld.md, "Detail textures".

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "assets/model.h"

namespace nmsc::assets::wld {

// Fills m.detail_layers and m.detail_palette from one BMINFO's names (the diffuse first), as the loader types them.
// A later call (a later frame of the sprite) replaces an earlier one, as ResolveTransparency's frame loop does.
void apply_detail_names(const std::vector<std::string>& names, Material& m);

// A palette map's pixel indices, `width` x `height`, bottom row first: v = 0 is the BMP's bottom row (WLD meshes
// address BMP rows from the bottom, assets::TextureOptions::wld_bmp_rows), the first row stored in the usual
// bottom-up BMP and the last in a top-down one (negative height).
struct PaletteMap {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> pixels;
};

// An uncompressed 8-bit BMP's indices; nullopt for anything else.
std::optional<PaletteMap> decode_palette_map(std::span<const std::byte> bmp);

// The detail entry the map gives a vertex at `uv`, or -1 when its pixel is not below `entries`. As 0x10019780
// samples it: x = round(u W) and y = round(v H) (x87 round to nearest even, 64-bit, kept as 32 unsigned bits),
// each modulo the size as unsigned, the pixel read as a signed char and compared unsigned.
int palette_entry(const PaletteMap& map, glm::vec2 uv, std::size_t entries);

// The UV scale of palette entry `scale` (BuildRegionVertices' param 14: the entry's integer scale x 18).
inline float palette_uv_scale(float scale) { return scale * 18.0f; }

// One detail pass batch of a region: a layer's triangles (index triples into the mesh) and their box in x and y
// (min x, max x, min y, max y), which the region's draw (CRegion vf4's draw path, 0x10018e2e) measures the camera
// against: the batch is drawn while the distance outside the box along x plus along y is below
// min(600, FogEnd x 1.414), with TEXTUREFACTOR alpha min(160, round((1 - d / 600) x 255)) (shaders/zone.vert).
struct DetailBatch {
    std::size_t layer = 0;
    glm::vec4 box{0.0f};
    std::vector<std::uint32_t> indices;
};

// The detail batches of material slot `material` of a region mesh, as 0x10019780 builds them. A single detail is
// one batch of all the material's triangles. With a palette map, the material's triangles are taken in reverse order
// (the builder prepends them to a list) and each goes into the batch of every entry one of its vertices has (below
// the layer count): the most recent batch of that entry whose box, grown by the triangle, stays under 150 units
// across in x and in y, else a new one. Batches come newest first, as the builder draws them.
std::vector<DetailBatch> detail_batches(const Mesh& mesh, std::uint32_t material, const PaletteMap* map);

} // namespace nmsc::assets::wld
