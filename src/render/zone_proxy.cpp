#include "render/zone_proxy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <unordered_map>

#include "core/byte_io.h"
#include "render/horizon.h"
#include "render/tree_impostor.h"
#include "render/zone_renderer.h"

namespace hailstone::render {

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

std::size_t at(int x, int y, int nx) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(nx) + static_cast<std::size_t>(x);
}

// The median of each channel of RGBA8 colours, 0xff808080 for none.
std::uint32_t median_rgba(const std::vector<std::uint32_t>& colors) {
    if (colors.empty()) return 0xff808080u;
    std::uint32_t out = 0;
    std::vector<std::uint32_t> channel(colors.size());
    for (std::uint32_t k = 0; k < 4; ++k) {
        for (std::size_t i = 0; i < colors.size(); ++i) channel[i] = (colors[i] >> (8 * k)) & 0xffu;
        const auto mid = channel.begin() + static_cast<std::ptrdiff_t>(channel.size() / 2);
        std::nth_element(channel.begin(), mid, channel.end());
        out |= *mid << (8 * k);
    }
    return out;
}

// An uncompressed RGBA DDS (as the EQG loader makes its baked tiles), which the zone renderer reads as any texture.
std::vector<std::byte> dds_rgba(const assets::Image& image) {
    ByteWriter out;
    out.le<std::uint32_t>(0x20534444u); // "DDS "
    out.le<std::uint32_t>(124);
    out.le<std::uint32_t>(0x1 | 0x2 | 0x4 | 0x8 | 0x1000);
    out.le<std::uint32_t>(image.height);
    out.le<std::uint32_t>(image.width);
    out.le<std::uint32_t>(image.width * 4);
    out.le<std::uint32_t>(0);
    out.le<std::uint32_t>(1);
    out.zeros(44);
    out.le<std::uint32_t>(32);
    out.le<std::uint32_t>(0x40 | 0x1);
    out.le<std::uint32_t>(0);
    out.le<std::uint32_t>(32);
    out.le<std::uint32_t>(0x00ff0000u);
    out.le<std::uint32_t>(0x0000ff00u);
    out.le<std::uint32_t>(0x000000ffu);
    out.le<std::uint32_t>(0xff000000u);
    out.le<std::uint32_t>(0x1000);
    out.zeros(16);
    const auto& px = image.mips.front();
    for (std::size_t i = 0; i + 3 < px.size(); i += 4) {
        out.le<std::uint8_t>(std::to_integer<std::uint8_t>(px[i + 2]));
        out.le<std::uint8_t>(std::to_integer<std::uint8_t>(px[i + 1]));
        out.le<std::uint8_t>(std::to_integer<std::uint8_t>(px[i]));
        out.le<std::uint8_t>(std::to_integer<std::uint8_t>(px[i + 3]));
    }
    return out.take();
}

bool finite2(glm::vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }

} // namespace

ZoneProxy bake_zone_proxy(const assets::Zone& zone, std::uint64_t key, const std::atomic<bool>* cancelled) {
    ZoneProxy out;
    out.zone = zone.name;
    out.key = key;
    auto stop = [&] { return cancelled && cancelled->load(); };
    const glm::vec2 bmin(zone.bounds_min), bmax(zone.bounds_max);
    if (!finite2(bmin) || !finite2(bmax) || !(bmax.x > bmin.x) || !(bmax.y > bmin.y)) return out;
    const glm::vec2 extent = bmax - bmin;
    float texel = std::max({kProxyTexel, extent.x / static_cast<float>(kProxyMaxTexture),
                            extent.y / static_cast<float>(kProxyMaxTexture)});
    const float cell = std::max(kProxyCell, 2.0f * texel);
    texel = cell / 2.0f; // two texels a cell, so the walls' strip (a texel a cell) fits under the picture
    out.cell = cell;
    out.texel = texel;
    out.lo = glm::floor(bmin / cell) * cell;
    out.nx = static_cast<int>(std::ceil((bmax.x - out.lo.x) / cell));
    out.ny = static_cast<int>(std::ceil((bmax.y - out.lo.y) / cell));
    if (out.nx < 2 || out.ny < 2) return out;
    const int w = out.nx * 2, h = out.ny * 2;
    const auto texels = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    const auto cells = static_cast<std::size_t>(out.nx) * static_cast<std::size_t>(out.ny);
    std::vector<float> zbuf(texels, -std::numeric_limits<float>::max());
    std::vector<std::array<std::uint8_t, 3>> rgb(texels, {0, 0, 0});
    // The walls' colour per cell: the steep triangles' texels summed where they stand.
    std::vector<glm::vec4> side(cells, glm::vec4(0.0f));

    // The game's own pictures (not a texture pack's), as their UVs read them.
    std::map<std::string, std::unique_ptr<assets::Image>> pictures;
    auto lookup = [&](const assets::Material& m) -> const assets::Image* {
        if (m.frames.empty()) return nullptr;
        const bool masked = m.blend == assets::BlendMode::Masked;
        const std::string name = m.frames.front() + (masked ? "#m" : "");
        auto [it, fresh] = pictures.try_emplace(name);
        if (fresh) {
            if (auto data = zone.read_file(m.frames.front())) {
                auto image = assets::decode_texture(*data, {.palette_index0_transparent = masked, .wld_bmp_rows = true});
                if (image && image->format != assets::PixelFormat::RGBA8) image = assets::to_rgba8(*image);
                if (image && image->format == assets::PixelFormat::RGBA8 && !image->mips.empty() && image->width > 0 &&
                    image->height > 0 &&
                    image->mips[0].size() >= static_cast<std::size_t>(image->width) * image->height * 4)
                    it->second = std::make_unique<assets::Image>(std::move(*image));
            }
        }
        return it->second.get();
    };
    auto sample = [](const assets::Image& image, glm::vec2 uv, std::array<std::uint8_t, 4>& o) {
        const float u = uv.x - std::floor(uv.x), v = uv.y - std::floor(uv.y);
        if (!std::isfinite(u) || !std::isfinite(v)) return false;
        const auto x = std::min(static_cast<std::uint32_t>(u * static_cast<float>(image.width)), image.width - 1);
        const auto y = std::min(static_cast<std::uint32_t>(v * static_cast<float>(image.height)), image.height - 1);
        const std::size_t i = (static_cast<std::size_t>(y) * image.width + x) * 4;
        for (std::size_t k = 0; k < 4; ++k) o[k] = std::to_integer<std::uint8_t>(image.mips[0][i + k]);
        return true;
    };
    // One triangle from above: the highest surface wins each texel. A steep one (a wall) adds its colour to the walls'
    // of the cells it stands in.
    auto raster = [&](const glm::vec3 (&p)[3], const glm::vec2 (&uv)[3], const assets::Image& image, bool masked) {
        const glm::vec3 n = glm::cross(p[1] - p[0], p[2] - p[0]);
        const float len = glm::length(n);
        if (!(len > 1e-6f)) return;
        if (std::abs(n.z) < 0.5f * len) {
            static constexpr float kAt[7][2] = {{1.0f / 3, 1.0f / 3}, {0.6f, 0.2f}, {0.2f, 0.6f}, {0.2f, 0.2f},
                                                {0.45f, 0.45f}, {0.1f, 0.45f}, {0.45f, 0.1f}};
            for (const auto& b : kAt) {
                const float b2 = 1.0f - b[0] - b[1];
                const glm::vec3 q = b[0] * p[0] + b[1] * p[1] + b2 * p[2];
                const glm::vec2 c = (glm::vec2(q) - out.lo) / cell;
                if (!(c.x >= 0.0f && c.y >= 0.0f && c.x < static_cast<float>(out.nx) && c.y < static_cast<float>(out.ny)))
                    continue;
                std::array<std::uint8_t, 4> t{};
                if (!sample(image, b[0] * uv[0] + b[1] * uv[1] + b2 * uv[2], t)) continue;
                if (masked && t[3] < 128) continue;
                side[at(static_cast<int>(c.x), static_cast<int>(c.y), out.nx)] += glm::vec4(t[0], t[1], t[2], 1.0f);
            }
            return;
        }
        glm::vec3 q[3];
        for (int k = 0; k < 3; ++k) q[k] = glm::vec3((glm::vec2(p[k]) - out.lo) / texel, p[k].z);
        const float area = (q[1].x - q[0].x) * (q[2].y - q[0].y) - (q[1].y - q[0].y) * (q[2].x - q[0].x);
        if (std::abs(area) < 1e-6f) return;
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({q[0].x, q[1].x, q[2].x}))));
        const int x1 = std::min(w - 1, static_cast<int>(std::ceil(std::max({q[0].x, q[1].x, q[2].x}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({q[0].y, q[1].y, q[2].y}))));
        const int y1 = std::min(h - 1, static_cast<int>(std::ceil(std::max({q[0].y, q[1].y, q[2].y}))));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float px = static_cast<float>(x) + 0.5f, py = static_cast<float>(y) + 0.5f;
                const float w0 = ((q[1].x - px) * (q[2].y - py) - (q[1].y - py) * (q[2].x - px)) / area;
                const float w1 = ((q[2].x - px) * (q[0].y - py) - (q[2].y - py) * (q[0].x - px)) / area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                const float z = w0 * q[0].z + w1 * q[1].z + w2 * q[2].z;
                const std::size_t i = at(x, y, w);
                if (z <= zbuf[i]) continue;
                std::array<std::uint8_t, 4> c{};
                if (!sample(image, w0 * uv[0] + w1 * uv[1] + w2 * uv[2], c)) continue;
                if (masked && c[3] < 128) continue;
                zbuf[i] = z;
                rgb[i] = {c[0], c[1], c[2]};
            }
    };
    auto draws = [](const assets::Material& m) {
        return m.blend != assets::BlendMode::Invisible && m.blend != assets::BlendMode::Additive && !m.frames.empty() &&
               m.eqg_shader != assets::EqgShader::Water && m.eqg_shader != assets::EqgShader::Lava &&
               m.eqg_shader != assets::EqgShader::LavaH;
    };
    // A level surface with a liquid region just under it and not just over it is the water's (the land draws it).
    auto liquid_region = [&](const glm::vec3& p) {
        if (zone.regions.empty()) return false;
        const std::uint32_t r = zone.regions.region_at(p);
        if (r == 0 || r > zone.regions.region_count()) return false;
        using T = assets::RegionType;
        const auto t = static_cast<T>(zone.regions.info(r).flags & 0xffu);
        return t == T::Water || t == T::Slime || t == T::FreezingWater || t == T::Water2 || t == T::Water3 || t == T::Lava;
    };
    std::vector<std::uint32_t> terrain_colors;
    auto raster_mesh = [&](const assets::Mesh& mesh, const glm::mat4& transform, const std::vector<std::uint8_t>* liquid) {
        for (const auto& part : mesh.parts) {
            if (part.material >= mesh.materials.size()) continue;
            const assets::Material& m = mesh.materials[part.material];
            if (!draws(m)) continue;
            const assets::Image* image = lookup(m);
            if (!image) continue;
            const bool masked = m.blend == assets::BlendMode::Masked;
            for (std::uint32_t i = 0; i + 2 < part.index_count; i += 3) {
                const std::size_t tri = (static_cast<std::size_t>(part.first_index) + i) / 3;
                if (liquid && tri < liquid->size() && (*liquid)[tri]) continue;
                glm::vec3 p[3];
                glm::vec2 uv[3];
                bool ok = true;
                for (std::uint32_t k = 0; k < 3; ++k) {
                    const std::size_t idx = static_cast<std::size_t>(part.first_index) + i + k;
                    if (idx >= mesh.indices.size() || mesh.indices[idx] >= mesh.vertices.size()) {
                        ok = false;
                        break;
                    }
                    const auto& v = mesh.vertices[mesh.indices[idx]];
                    p[k] = glm::vec3(transform * glm::vec4(v.position, 1.0f));
                    uv[k] = v.uv;
                    if (liquid && terrain_colors.size() < 65536) terrain_colors.push_back(v.color);
                }
                if (!ok) continue;
                if (liquid) {
                    const glm::vec3 n = glm::cross(p[1] - p[0], p[2] - p[0]);
                    const float len = glm::length(n);
                    const glm::vec3 c = (p[0] + p[1] + p[2]) / 3.0f;
                    if (len > 1e-6f && std::abs(n.z) >= 0.8f * len && liquid_region(c - glm::vec3(0.0f, 0.0f, 1.5f)) &&
                        !liquid_region(c + glm::vec3(0.0f, 0.0f, 1.5f)))
                        continue;
                }
                raster(p, uv, *image, masked);
            }
        }
    };
    const auto liquid = classic_liquid_triangles(zone);
    for (std::size_t mi = 0; mi < zone.terrain.size(); ++mi) {
        if (stop()) return {};
        raster_mesh(zone.terrain[mi], glm::mat4(1.0f), mi < liquid.size() ? &liquid[mi] : nullptr);
    }
    out.light = median_rgba(terrain_colors);
    // The placed objects; the trees apart, as pictures where they stand, and the small plants left out.
    struct Kind {
        std::uint32_t index = 0xffffffffu;
    };
    std::unordered_map<std::string, int> plant; // 0 not a plant, 1 a plant
    std::map<std::string, Kind> kinds;
    std::vector<std::pair<std::string, const assets::Placement*>> tree_spots;
    for (std::size_t pi = 0; pi < zone.placements.size(); ++pi) {
        if (pi % 256 == 0 && stop()) return {};
        const auto& p = zone.placements[pi];
        auto it = zone.objects.find(p.actor);
        if (it == zone.objects.end()) continue;
        auto [known, fresh] = plant.try_emplace(p.actor, 0);
        if (fresh && is_plant(p.actor, it->second)) known->second = 1;
        if (known->second == 1) {
            if (mesh_height(it->second) * glm::length(glm::vec3(p.transform[2])) >= kLandTreeMinHeight) {
                kinds.try_emplace(p.actor);
                tree_spots.emplace_back(p.actor, &p);
            }
            continue;
        }
        raster_mesh(it->second, p.transform, nullptr);
    }
    for (auto& [actor, k] : kinds) {
        auto impostor = tree_impostor(zone.objects.at(actor), lookup, "tree");
        if (!impostor) continue;
        k.index = static_cast<std::uint32_t>(out.tree_kinds.size());
        out.tree_kinds.push_back({impostor->lo, impostor->hi, std::move(impostor->image)});
    }
    for (const auto& [actor, p] : tree_spots) {
        const Kind& k = kinds.at(actor);
        if (k.index == 0xffffffffu) continue;
        out.trees.push_back({p->transform, k.index, median_rgba(p->vertex_colors)});
    }
    if (stop()) return {};

    // The heights: each cell's highest texel, then "opened" (the lowest of each 3 x 3, then the highest of that), which
    // takes off what is narrower than three cells (posts, banners, a lamp) and keeps the rest.
    out.height.assign(cells, kNaN);
    for (int cy = 0; cy < out.ny; ++cy)
        for (int cx = 0; cx < out.nx; ++cx) {
            float top = -std::numeric_limits<float>::max();
            for (int y = cy * 2; y < cy * 2 + 2; ++y)
                for (int x = cx * 2; x < cx * 2 + 2; ++x) top = std::max(top, zbuf[at(x, y, w)]);
            if (top > -1e30f) out.height[at(cx, cy, out.nx)] = top;
        }
    auto pass = [&](const std::vector<float>& in, bool lowest) {
        std::vector<float> res = in;
        for (int y = 0; y < out.ny; ++y)
            for (int x = 0; x < out.nx; ++x) {
                if (!std::isfinite(in[at(x, y, out.nx)])) continue;
                float v = in[at(x, y, out.nx)];
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int px = x + dx, py = y + dy;
                        if (px < 0 || py < 0 || px >= out.nx || py >= out.ny) continue;
                        const float o = in[at(px, py, out.nx)];
                        if (std::isfinite(o)) v = lowest ? std::min(v, o) : std::max(v, o);
                    }
                res[at(x, y, out.nx)] = v;
            }
        return res;
    };
    out.height = pass(pass(out.height, true), false);

    // How far in from the edge of what it has each cell lies: to the nearest cell with nothing that the grid's border
    // reaches through cells with nothing (a canal or a harbour inside the zone is no edge), or the border (a two-pass
    // chamfer distance).
    std::vector<float> inner(cells, 1e9f);
    {
        std::vector<std::uint8_t> outside(cells, 0);
        std::vector<std::pair<int, int>> stack;
        auto seed = [&](int x, int y) {
            const std::size_t i = at(x, y, out.nx);
            if (outside[i] || std::isfinite(out.height[i])) return;
            outside[i] = 1;
            stack.emplace_back(x, y);
        };
        for (int x = 0; x < out.nx; ++x) {
            seed(x, 0);
            seed(x, out.ny - 1);
        }
        for (int y = 0; y < out.ny; ++y) {
            seed(0, y);
            seed(out.nx - 1, y);
        }
        while (!stack.empty()) {
            const auto [x, y] = stack.back();
            stack.pop_back();
            if (x > 0) seed(x - 1, y);
            if (y > 0) seed(x, y - 1);
            if (x + 1 < out.nx) seed(x + 1, y);
            if (y + 1 < out.ny) seed(x, y + 1);
        }
        for (int y = 0; y < out.ny; ++y)
            for (int x = 0; x < out.nx; ++x)
                if (outside[at(x, y, out.nx)] || x == 0 || y == 0 || x == out.nx - 1 || y == out.ny - 1)
                    inner[at(x, y, out.nx)] = 0.0f;
        const float d1 = cell, d2 = cell * 1.41421356f;
        for (int y = 0; y < out.ny; ++y)
            for (int x = 0; x < out.nx; ++x) {
                float& v = inner[at(x, y, out.nx)];
                if (x > 0) v = std::min(v, inner[at(x - 1, y, out.nx)] + d1);
                if (y > 0) v = std::min(v, inner[at(x, y - 1, out.nx)] + d1);
                if (x > 0 && y > 0) v = std::min(v, inner[at(x - 1, y - 1, out.nx)] + d2);
                if (x + 1 < out.nx && y > 0) v = std::min(v, inner[at(x + 1, y - 1, out.nx)] + d2);
            }
        for (int y = out.ny - 1; y >= 0; --y)
            for (int x = out.nx - 1; x >= 0; --x) {
                float& v = inner[at(x, y, out.nx)];
                if (x + 1 < out.nx) v = std::min(v, inner[at(x + 1, y, out.nx)] + d1);
                if (y + 1 < out.ny) v = std::min(v, inner[at(x, y + 1, out.nx)] + d1);
                if (x + 1 < out.nx && y + 1 < out.ny) v = std::min(v, inner[at(x + 1, y + 1, out.nx)] + d2);
                if (x > 0 && y + 1 < out.ny) v = std::min(v, inner[at(x - 1, y + 1, out.nx)] + d2);
            }
    }

    // Which cells are drawn. Past the edge ring, every cell with a height. In it, a cell standing at most
    // kProxyWallRise over the zone's own ground near it: the median of the cells past the ring in blocks of 16 x 16
    // cells, the nearest block that has some within three blocks, else the whole zone's median.
    constexpr int kBlock = 16;
    const int bx = (out.nx + kBlock - 1) / kBlock, by = (out.ny + kBlock - 1) / kBlock;
    std::vector<float> block_ground(static_cast<std::size_t>(bx) * static_cast<std::size_t>(by), kNaN);
    std::vector<float> all_deep;
    {
        std::vector<std::vector<float>> deep(block_ground.size());
        for (int y = 0; y < out.ny; ++y)
            for (int x = 0; x < out.nx; ++x) {
                const std::size_t i = at(x, y, out.nx);
                if (!std::isfinite(out.height[i]) || inner[i] < kProxyEdgeRing) continue;
                deep[at(x / kBlock, y / kBlock, bx)].push_back(out.height[i]);
                all_deep.push_back(out.height[i]);
            }
        for (std::size_t b = 0; b < deep.size(); ++b) {
            auto& v = deep[b];
            if (v.empty()) continue;
            std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
            block_ground[b] = v[v.size() / 2];
        }
    }
    float usual = std::numeric_limits<float>::max();
    if (!all_deep.empty()) {
        std::nth_element(all_deep.begin(), all_deep.begin() + static_cast<std::ptrdiff_t>(all_deep.size() / 2),
                         all_deep.end());
        usual = all_deep[all_deep.size() / 2];
    }
    auto ground_near = [&](int x, int y) {
        const int cx = x / kBlock, cy = y / kBlock;
        float best = usual, nearest = std::numeric_limits<float>::max();
        for (int dy = -3; dy <= 3; ++dy)
            for (int dx = -3; dx <= 3; ++dx) {
                const int px = cx + dx, py = cy + dy;
                if (px < 0 || py < 0 || px >= bx || py >= by) continue;
                const float g = block_ground[at(px, py, bx)];
                if (!std::isfinite(g)) continue;
                const auto d = static_cast<float>(dx * dx + dy * dy);
                if (d < nearest) {
                    nearest = d;
                    best = g;
                }
            }
        return best;
    };
    out.drawn.assign(cells, 0);
    for (int y = 0; y < out.ny; ++y)
        for (int x = 0; x < out.nx; ++x) {
            const std::size_t i = at(x, y, out.nx);
            if (!std::isfinite(out.height[i])) continue;
            out.drawn[i] = inner[i] >= kProxyEdgeRing || out.height[i] <= ground_near(x, y) + kProxyWallRise;
        }
    // The pieces cut off from the rest that are smaller than kProxyMinIsland cells go.
    {
        std::vector<int> label(cells, -1);
        std::vector<std::size_t> stack, members;
        for (std::size_t s = 0; s < cells; ++s) {
            if (!out.drawn[s] || label[s] >= 0) continue;
            members.clear();
            stack.assign(1, s);
            label[s] = 0;
            while (!stack.empty()) {
                const std::size_t i = stack.back();
                stack.pop_back();
                members.push_back(i);
                const int x = static_cast<int>(i % static_cast<std::size_t>(out.nx));
                const int y = static_cast<int>(i / static_cast<std::size_t>(out.nx));
                const int nb[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
                for (const auto& q : nb) {
                    if (q[0] < 0 || q[1] < 0 || q[0] >= out.nx || q[1] >= out.ny) continue;
                    const std::size_t j = at(q[0], q[1], out.nx);
                    if (!out.drawn[j] || label[j] >= 0) continue;
                    label[j] = 0;
                    stack.push_back(j);
                }
            }
            if (members.size() < kProxyMinIsland)
                for (std::size_t i : members) out.drawn[i] = 0;
        }
    }

    // The picture: the colour seen from above (the mean where nothing was drawn, so the filtering does not darken the
    // edges), then the walls' strip: a cell's own walls' colour, else its colour from above darkened.
    glm::dvec3 sum(0.0);
    double n = 0.0;
    for (std::size_t i = 0; i < texels; ++i)
        if (zbuf[i] > -1e30f) {
            sum += glm::dvec3(rgb[i][0], rgb[i][1], rgb[i][2]);
            n += 1.0;
        }
    if (n == 0.0) return {};
    const glm::dvec3 mean = sum / n;
    out.top_width = static_cast<std::uint32_t>(w);
    out.top_height = static_cast<std::uint32_t>(h);
    out.picture.width = static_cast<std::uint32_t>(w);
    out.picture.height = static_cast<std::uint32_t>(h) + kProxyStripGap + static_cast<std::uint32_t>(out.ny);
    out.picture.format = assets::PixelFormat::RGBA8;
    std::vector<std::byte> px(static_cast<std::size_t>(out.picture.width) * out.picture.height * 4, std::byte{0xff});
    auto put = [&](std::size_t texel_index, glm::vec3 c) {
        for (std::size_t k = 0; k < 3; ++k)
            px[texel_index * 4 + k] =
                static_cast<std::byte>(static_cast<std::uint8_t>(std::lround(std::clamp(c[static_cast<glm::length_t>(k)], 0.0f, 255.0f))));
    };
    for (std::size_t i = 0; i < texels; ++i)
        put(i, zbuf[i] > -1e30f ? glm::vec3(rgb[i][0], rgb[i][1], rgb[i][2]) : glm::vec3(mean));
    for (std::uint32_t g = 0; g < kProxyStripGap; ++g)
        for (std::uint32_t x = 0; x < out.picture.width; ++x)
            put(static_cast<std::size_t>(out.top_height + g) * out.picture.width + x, glm::vec3(mean) * 0.7f);
    for (int y = 0; y < out.ny; ++y)
        for (int x = 0; x < out.nx; ++x) {
            const glm::vec4 s = side[at(x, y, out.nx)];
            glm::vec3 c;
            if (s.w > 0.0f) {
                c = glm::vec3(s) / s.w * 0.85f;
            } else {
                const std::size_t t = at(x * 2, y * 2, w);
                c = (zbuf[t] > -1e30f ? glm::vec3(rgb[t][0], rgb[t][1], rgb[t][2]) : glm::vec3(mean)) * 0.6f;
            }
            put(static_cast<std::size_t>(out.top_height + kProxyStripGap + static_cast<std::uint32_t>(y)) *
                        out.picture.width +
                    static_cast<std::size_t>(x),
                c);
        }
    out.picture.mips.push_back(std::move(px));
    return out;
}

// ---- The cache ----

namespace {
constexpr std::uint32_t kProxyMagic = 0x31585048u; // "HPX1"
constexpr std::uint32_t kProxyVersion = 1;
constexpr std::uint32_t kMaxSide = 4096;     // cells a side
constexpr std::uint32_t kMaxPicture = 4096 + kProxyStripGap + kMaxSide;
constexpr std::uint32_t kMaxKinds = 64;
constexpr std::uint32_t kMaxTrees = 200000;
constexpr std::size_t kMaxName = 64;

void write_image(ByteWriter& w, const assets::Image& image) {
    w.le(image.width);
    w.le(image.height);
    w.bytes(image.mips.front());
}

std::optional<assets::Image> read_image(ByteReader& r, std::uint32_t max_w, std::uint32_t max_h) {
    assets::Image image;
    image.width = r.le<std::uint32_t>();
    image.height = r.le<std::uint32_t>();
    if (image.width == 0 || image.height == 0 || image.width > max_w || image.height > max_h) return std::nullopt;
    const std::size_t n = static_cast<std::size_t>(image.width) * image.height * 4;
    if (r.remaining() < n) return std::nullopt;
    const auto bytes = r.bytes(n);
    image.mips.emplace_back(bytes.begin(), bytes.end());
    return image;
}
} // namespace

std::vector<std::byte> write_zone_proxy(const ZoneProxy& p) {
    ByteWriter w;
    w.le(kProxyMagic);
    w.le(kProxyVersion);
    const std::size_t name = std::min(p.zone.size(), kMaxName);
    w.le(static_cast<std::uint16_t>(name));
    w.bytes(std::as_bytes(std::span(p.zone.data(), name)));
    w.le(p.key);
    const bool ok = p.valid();
    w.le(static_cast<std::uint8_t>(ok ? 1 : 0));
    if (!ok) return w.take();
    w.f32(p.lo.x);
    w.f32(p.lo.y);
    w.f32(p.cell);
    w.f32(p.texel);
    w.le(static_cast<std::uint32_t>(p.nx));
    w.le(static_cast<std::uint32_t>(p.ny));
    w.le(p.light);
    w.le(p.top_width);
    w.le(p.top_height);
    for (float v : p.height) w.f32(v);
    for (std::uint8_t d : p.drawn) w.le(d);
    write_image(w, p.picture);
    w.le(static_cast<std::uint32_t>(p.tree_kinds.size()));
    for (const auto& k : p.tree_kinds) {
        for (float v : {k.lo.x, k.lo.y, k.lo.z, k.hi.x, k.hi.y, k.hi.z}) w.f32(v);
        write_image(w, k.image);
    }
    w.le(static_cast<std::uint32_t>(p.trees.size()));
    for (const auto& t : p.trees) {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 3; ++r) w.f32(t.transform[c][r]);
        w.le(t.kind);
        w.le(t.tint);
    }
    return w.take();
}

std::optional<std::pair<std::string, std::uint64_t>> read_zone_proxy_header(std::span<const std::byte> data) {
    try {
        ByteReader r(data);
        if (r.le<std::uint32_t>() != kProxyMagic || r.le<std::uint32_t>() != kProxyVersion) return std::nullopt;
        const auto name = r.le<std::uint16_t>();
        if (name > kMaxName) return std::nullopt;
        const auto nb = r.bytes(name);
        std::string zone(reinterpret_cast<const char*>(nb.data()), nb.size());
        return std::pair{std::move(zone), r.le<std::uint64_t>()};
    } catch (const std::out_of_range&) {
        return std::nullopt;
    }
}

std::optional<ZoneProxy> read_zone_proxy(std::span<const std::byte> data) {
    try {
        ByteReader r(data);
        if (r.le<std::uint32_t>() != kProxyMagic || r.le<std::uint32_t>() != kProxyVersion) return std::nullopt;
        ZoneProxy p;
        const auto name = r.le<std::uint16_t>();
        if (name > kMaxName) return std::nullopt;
        const auto nb = r.bytes(name);
        p.zone.assign(reinterpret_cast<const char*>(nb.data()), nb.size());
        p.key = r.le<std::uint64_t>();
        if (r.le<std::uint8_t>() != 1) return p; // nothing to draw, kept so it is not baked again
        p.lo.x = r.f32();
        p.lo.y = r.f32();
        p.cell = r.f32();
        p.texel = r.f32();
        const auto nx = r.le<std::uint32_t>(), ny = r.le<std::uint32_t>();
        p.light = r.le<std::uint32_t>();
        p.top_width = r.le<std::uint32_t>();
        p.top_height = r.le<std::uint32_t>();
        if (!finite2(p.lo) || !(p.cell >= 1.0f && p.cell <= 4096.0f) || !(p.texel >= 0.5f && p.texel <= p.cell) ||
            nx < 2 || ny < 2 || nx > kMaxSide || ny > kMaxSide || p.top_width == 0 || p.top_height == 0 ||
            p.top_width > kMaxPicture || p.top_height > kMaxPicture)
            return std::nullopt;
        p.nx = static_cast<int>(nx);
        p.ny = static_cast<int>(ny);
        const std::size_t n = static_cast<std::size_t>(nx) * ny;
        if (r.remaining() < n * 5) return std::nullopt;
        p.height.resize(n);
        for (float& v : p.height) {
            v = r.f32();
            if (std::isinf(v)) v = kNaN;
        }
        p.drawn.resize(n);
        for (std::uint8_t& d : p.drawn) d = r.le<std::uint8_t>() != 0 ? 1 : 0;
        for (std::size_t i = 0; i < n; ++i)
            if (!std::isfinite(p.height[i])) p.drawn[i] = 0;
        auto picture = read_image(r, kMaxPicture, kMaxPicture);
        if (!picture || picture->width < nx || picture->width < p.top_width ||
            picture->height < p.top_height + kProxyStripGap + ny)
            return std::nullopt;
        p.picture = std::move(*picture);
        const auto kinds = r.le<std::uint32_t>();
        if (kinds > kMaxKinds) return std::nullopt;
        for (std::uint32_t k = 0; k < kinds; ++k) {
            ZoneProxy::TreeKind kind;
            float v[6];
            for (float& x : v) x = r.f32();
            kind.lo = {v[0], v[1], v[2]};
            kind.hi = {v[3], v[4], v[5]};
            for (float x : v)
                if (!std::isfinite(x)) return std::nullopt;
            auto image = read_image(r, kImpostorSize * 2, kImpostorSize);
            if (!image) return std::nullopt;
            kind.image = std::move(*image);
            p.tree_kinds.push_back(std::move(kind));
        }
        const auto trees = r.le<std::uint32_t>();
        if (trees > kMaxTrees || r.remaining() < static_cast<std::size_t>(trees) * 56) return std::nullopt;
        p.trees.reserve(trees);
        for (std::uint32_t i = 0; i < trees; ++i) {
            ZoneProxy::Tree t;
            for (int c = 0; c < 4; ++c)
                for (int row = 0; row < 3; ++row) {
                    const float v = r.f32();
                    if (!std::isfinite(v)) return std::nullopt;
                    t.transform[c][row] = v;
                }
            t.kind = r.le<std::uint32_t>();
            t.tint = r.le<std::uint32_t>();
            if (t.kind >= p.tree_kinds.size()) return std::nullopt;
            p.trees.push_back(t);
        }
        if (!r.empty()) return std::nullopt;
        return p;
    } catch (const std::out_of_range&) {
        return std::nullopt;
    }
}

// ---- Drawing it ----

int proxy_step(float distance) { return distance < 3000.0f ? 1 : distance < 6000.0f ? 2 : 4; }

float proxy_distance(const HorizonField* field, glm::vec2 p) {
    if (!field || field->nx <= 0 || field->ny <= 0 || field->dist.size() != static_cast<std::size_t>(field->nx) *
                                                                               static_cast<std::size_t>(field->ny))
        return std::numeric_limits<float>::max();
    float extra = 0.0f;
    const std::size_t i = field->square(p, extra);
    if (extra <= 0.0f && (field->covered(i) || !field->open[i])) return -1.0f;
    return std::max(field->dist[i] - 0.5f, 0.0f) * field->cell + extra;
}

ProxyZone proxy_zone(const ZoneProxy& z, const HorizonField* field, glm::vec3 offset,
                     std::span<const glm::vec4> keep) {
    ProxyZone out;
    if (!z.valid()) return out;
    const std::string picture = "hs_copy_" + z.zone + ".dds";
    out.zone.name = "copy:" + z.zone;
    out.zone.generated_files.emplace(picture, dds_rgba(z.picture));
    const glm::vec2 shift(offset);
    auto kept = [&](glm::vec2 p) {
        for (const glm::vec4& plane : keep)
            if (plane.x * p.x + plane.y * p.y + plane.w < 0.0f) return false;
        return true;
    };
    // Per cell: how far past the current zone's edge, and whether it is drawn there.
    const auto cells = static_cast<std::size_t>(z.nx) * static_cast<std::size_t>(z.ny);
    std::vector<float> dist(cells, 0.0f);
    std::vector<std::uint8_t> on(cells, 0);
    for (int y = 0; y < z.ny; ++y)
        for (int x = 0; x < z.nx; ++x) {
            const std::size_t i = at(x, y, z.nx);
            if (!z.drawn[i]) continue;
            const glm::vec2 p = z.lo + (glm::vec2(static_cast<float>(x), static_cast<float>(y)) + 0.5f) * z.cell + shift;
            dist[i] = proxy_distance(field, p);
            on[i] = dist[i] >= kProxyNear && kept(p);
        }
    const float pw = static_cast<float>(z.picture.width), ph = static_cast<float>(z.picture.height);
    const glm::vec2 top_size(static_cast<float>(z.top_width) * z.texel, static_cast<float>(z.top_height) * z.texel);
    auto top_uv = [&](glm::vec2 local) {
        const glm::vec2 t = (local - z.lo) / top_size;
        return glm::vec2(t.x * static_cast<float>(z.top_width) / pw, t.y * static_cast<float>(z.top_height) / ph);
    };
    auto side_uv = [&](int x, int y) {
        return glm::vec2((static_cast<float>(x) + 0.5f) / pw,
                         (static_cast<float>(z.top_height + kProxyStripGap) + static_cast<float>(y) + 0.5f) / ph);
    };
    auto lift = [&](std::size_t i) {
        const float t = std::clamp((dist[i] - kProxyNear) / kProxyFade, 0.0f, 1.0f);
        const float s = t * t * (3.0f - 2.0f * t);
        return kProxyRaise - (1.0f - s) * (kProxySink + kProxyRaise);
    };
    // The chunks and their steps.
    const int cx = (z.nx - 1 + kProxyChunk - 1) / kProxyChunk, cy = (z.ny - 1 + kProxyChunk - 1) / kProxyChunk;
    std::vector<int> steps(static_cast<std::size_t>(cx) * static_cast<std::size_t>(cy), 1);
    for (int j = 0; j < cy; ++j)
        for (int i = 0; i < cx; ++i) {
            const int mx = std::min(i * kProxyChunk + kProxyChunk / 2, z.nx - 1);
            const int my = std::min(j * kProxyChunk + kProxyChunk / 2, z.ny - 1);
            const glm::vec2 p =
                z.lo + (glm::vec2(static_cast<float>(mx), static_cast<float>(my)) + 0.5f) * z.cell + shift;
            const float d = proxy_distance(field, p);
            steps[at(i, j, cx)] = proxy_step(d < 0.0f ? 0.0f : d);
        }
    assets::Material material;
    material.name = picture;
    material.frames = {picture};
    material.two_sided = true;
    for (int j = 0; j < cy; ++j)
        for (int i = 0; i < cx; ++i) {
            const int s = steps[at(i, j, cx)];
            const int x0 = i * kProxyChunk, y0 = j * kProxyChunk;
            const int x1 = std::min(x0 + kProxyChunk, z.nx - 1), y1 = std::min(y0 + kProxyChunk, z.ny - 1);
            assets::Mesh mesh;
            mesh.name = picture;
            mesh.front_ccw = true;
            mesh.materials.push_back(material);
            std::map<std::pair<int, int>, std::uint32_t> made;
            auto local_of = [&](int x, int y) {
                return z.lo + (glm::vec2(static_cast<float>(x), static_cast<float>(y)) + 0.5f) * z.cell;
            };
            auto height_of = [&](int x, int y) { return z.height[at(x, y, z.nx)] + lift(at(x, y, z.nx)); };
            auto normal_of = [&](int x, int y) {
                auto hh = [&](int px, int py) {
                    px = std::clamp(px, 0, z.nx - 1);
                    py = std::clamp(py, 0, z.ny - 1);
                    const float v = z.height[at(px, py, z.nx)];
                    return std::isfinite(v) ? v : z.height[at(x, y, z.nx)];
                };
                return glm::normalize(glm::vec3(hh(x - s, y) - hh(x + s, y), hh(x, y - s) - hh(x, y + s),
                                                2.0f * static_cast<float>(s) * z.cell));
            };
            auto vertex = [&](int x, int y) {
                auto [it, fresh] = made.try_emplace({x, y}, 0u);
                if (!fresh) return it->second;
                assets::Vertex v;
                v.position = glm::vec3(local_of(x, y), height_of(x, y));
                v.normal = normal_of(x, y);
                v.uv = top_uv(local_of(x, y));
                v.color = z.light;
                it->second = static_cast<std::uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(v);
                return it->second;
            };
            // A wall's quad: its own four corners, each at the walls' colour of its own cell, so the colour runs across
            // a cliff face and the quads sharing a corner agree (one colour a quad, from its highest cell, drew a cliff
            // as a checkerboard of blocks once the copies reached the zone line).
            auto wall = [&](const int (&q)[4][2]) {
                const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                for (const auto& c : q) {
                    assets::Vertex v;
                    v.position = glm::vec3(local_of(c[0], c[1]), height_of(c[0], c[1]));
                    v.normal = normal_of(c[0], c[1]);
                    v.uv = side_uv(c[0], c[1]);
                    v.color = z.light;
                    mesh.vertices.push_back(v);
                }
                mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            };
            auto skirt = [&](int ax, int ay, int bx2, int by2) {
                const float drop = 20.0f * static_cast<float>(s);
                const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                for (const auto& c : {std::pair{ax, ay}, std::pair{bx2, by2}}) {
                    for (float dz : {0.0f, -drop}) {
                        assets::Vertex v;
                        v.position = glm::vec3(local_of(c.first, c.second), height_of(c.first, c.second) + dz);
                        v.normal = normal_of(c.first, c.second);
                        v.uv = top_uv(local_of(c.first, c.second));
                        v.color = z.light;
                        mesh.vertices.push_back(v);
                    }
                }
                mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 3, base, base + 3, base + 2});
            };
            auto coarser = [&](int ni, int nj) {
                if (ni < 0 || nj < 0 || ni >= cx || nj >= cy) return false;
                return steps[at(ni, nj, cx)] != s;
            };
            for (int y = y0; y < y1; y += s)
                for (int x = x0; x < x1; x += s) {
                    const int xs = std::min(x + s, x1), ys = std::min(y + s, y1);
                    const int q[4][2] = {{x, y}, {xs, y}, {xs, ys}, {x, ys}};
                    bool all = true;
                    float lo = std::numeric_limits<float>::max(), hi = -lo;
                    for (const auto& c : q) {
                        const std::size_t ci = at(c[0], c[1], z.nx);
                        all &= on[ci] != 0;
                        lo = std::min(lo, z.height[ci]);
                        hi = std::max(hi, z.height[ci]);
                    }
                    if (!all) continue;
                    out.triangles += 2;
                    if (hi - lo > 1.2f * static_cast<float>(std::max(xs - x, ys - y)) * z.cell) {
                        wall(q);
                    } else {
                        const std::uint32_t a = vertex(q[0][0], q[0][1]), b = vertex(q[1][0], q[1][1]),
                                            c = vertex(q[2][0], q[2][1]), d = vertex(q[3][0], q[3][1]);
                        mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
                    }
                    // Where the next chunk has another step, a skirt down under the seam.
                    if (y == y0 && coarser(i, j - 1)) skirt(x, y, xs, y), out.triangles += 2;
                    if (ys == y1 && coarser(i, j + 1)) skirt(xs, ys, x, ys), out.triangles += 2;
                    if (x == x0 && coarser(i - 1, j)) skirt(x, ys, x, y), out.triangles += 2;
                    if (xs == x1 && coarser(i + 1, j)) skirt(xs, y, xs, ys), out.triangles += 2;
                }
            if (mesh.indices.empty()) continue;
            assets::MeshPart part;
            part.index_count = static_cast<std::uint32_t>(mesh.indices.size());
            mesh.parts.push_back(part);
            out.zone.terrain.push_back(std::move(mesh));
        }
    // Its trees, where it is drawn past its band.
    for (std::size_t k = 0; k < z.tree_kinds.size(); ++k) {
        const std::string name = "hs_copy_" + z.zone + "_tree" + std::to_string(k) + ".dds";
        out.zone.generated_files.emplace(name, dds_rgba(z.tree_kinds[k].image));
        out.zone.objects.emplace("HS_COPY_TREE" + std::to_string(k),
                                 impostor_cards(z.tree_kinds[k].lo, z.tree_kinds[k].hi, name));
    }
    for (const auto& t : z.trees) {
        const glm::vec2 local(t.transform[3]);
        const glm::vec2 c = (local - z.lo) / z.cell;
        if (!(c.x >= 0.0f && c.y >= 0.0f && c.x < static_cast<float>(z.nx) && c.y < static_cast<float>(z.ny))) continue;
        const std::size_t ci = at(static_cast<int>(c.x), static_cast<int>(c.y), z.nx);
        if (!on[ci] || dist[ci] < kProxyNear + kProxyFade) continue;
        assets::Placement p;
        p.actor = "HS_COPY_TREE" + std::to_string(t.kind);
        p.transform = t.transform;
        p.transform[3].z += kProxyRaise;
        p.vertex_colors.assign(8, t.tint);
        out.zone.placements.push_back(std::move(p));
        ++out.trees;
    }
    // Its box.
    out.zone.bounds_min = glm::vec3(z.lo, std::numeric_limits<float>::max());
    out.zone.bounds_max = glm::vec3(z.lo + glm::vec2(static_cast<float>(z.nx), static_cast<float>(z.ny)) * z.cell,
                                    -std::numeric_limits<float>::max());
    for (float v : z.height)
        if (std::isfinite(v)) {
            out.zone.bounds_min.z = std::min(out.zone.bounds_min.z, v);
            out.zone.bounds_max.z = std::max(out.zone.bounds_max.z, v);
        }
    if (out.zone.bounds_min.z > out.zone.bounds_max.z) out.zone.bounds_min.z = out.zone.bounds_max.z = 0.0f;

    // The land's squares it covers whole past its band: every point of the square, a cell apart, on a drawn cell.
    if (field && field->xs.size() > 1 && field->ys.size() > 1) {
        const glm::vec2 lo = z.lo + shift, hi = lo + glm::vec2(static_cast<float>(z.nx), static_cast<float>(z.ny)) * z.cell;
        auto covered = [&](glm::vec2 p) {
            const glm::vec2 c = (p - shift - z.lo) / z.cell;
            if (!(c.x >= 0.0f && c.y >= 0.0f && c.x < static_cast<float>(z.nx) && c.y < static_cast<float>(z.ny)))
                return false;
            const std::size_t ci = at(static_cast<int>(c.x), static_cast<int>(c.y), z.nx);
            return on[ci] && dist[ci] >= kProxyNear + kProxyFade;
        };
        const auto c0 = static_cast<std::size_t>(std::max<std::ptrdiff_t>(
            std::upper_bound(field->xs.begin(), field->xs.end(), lo.x) - field->xs.begin() - 1, 0));
        const auto r0 = static_cast<std::size_t>(std::max<std::ptrdiff_t>(
            std::upper_bound(field->ys.begin(), field->ys.end(), lo.y) - field->ys.begin() - 1, 0));
        for (std::size_t r = r0; r + 1 < field->ys.size() && field->ys[r] < hi.y; ++r)
            for (std::size_t c = c0; c + 1 < field->xs.size() && field->xs[c] < hi.x; ++c) {
                bool whole = true;
                const float step = z.cell;
                for (float y = field->ys[r]; whole && y <= field->ys[r + 1]; y += step)
                    for (float x = field->xs[c]; whole && x <= field->xs[c + 1]; x += step)
                        whole = covered({std::min(x, field->xs[c + 1]), std::min(y, field->ys[r + 1])});
                if (whole)
                    out.footprint.push_back(static_cast<std::int64_t>(r) * static_cast<std::int64_t>(field->xs.size()) +
                                            static_cast<std::int64_t>(c));
            }
        std::sort(out.footprint.begin(), out.footprint.end());
    }
    return out;
}

} // namespace hailstone::render
