#include "assets/zone_flora.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

#include "assets/flora.h"
#include "assets/pfs.h"
#include "assets/texture.h"
#include "assets/zone.h"
#include "core/ascii_case.h"
#include "core/float_chars.h"
#include "core/game_dir.h"
#include "core/log.h"

namespace nmsc::assets {

namespace {

std::vector<std::string_view> words(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
        std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') ++i;
        if (i > start) out.push_back(line.substr(start, i - start));
    }
    return out;
}

bool number(std::string_view s, float& out) {
    float v = 0.0f;
    auto r = float_from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc() || r.ptr != s.data() + s.size() || !std::isfinite(v)) return false;
    out = v;
    return true;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = ascii_lower(c);
    return out;
}

// The file name of a material's texture: a frame renamed "<archive>/<file>" (wld_zone.cpp use_own_textures) is
// <file>.
std::string_view texture_of(const Material& m) {
    if (m.frames.empty()) return {};
    std::string_view f = m.frames.front();
    if (auto slash = f.find_last_of('/'); slash != std::string_view::npos) f = f.substr(slash + 1);
    return f;
}

float unit(MsvcRand& rand) { return static_cast<float>(rand()) / 32767.0f; }

// A triangle's own sequence (Hailstone's own seed, so it grows the same plants at every load).
std::uint32_t triangle_seed(std::size_t mesh, std::size_t triangle) {
    return static_cast<std::uint32_t>(mesh) * 2654435761u ^ static_cast<std::uint32_t>(triangle) * 40503u ^
           0x5bd1e995u;
}
} // namespace

ZoneFloraRules parse_zone_flora_rules(std::string_view text, std::vector<std::string>* errors) {
    ZoneFloraRules rules;
    std::size_t line_no = 0;
    auto fail = [&](std::string_view why) {
        if (errors) errors->push_back("line " + std::to_string(line_no) + ": " + std::string(why));
    };
    while (!text.empty()) {
        ++line_no;
        std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (auto hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
        auto w = words(line);
        if (w.empty()) continue;
        if (iequals(w[0], "donor") && w.size() == 2) {
            rules.donor = lower(w[1]);
        } else if (iequals(w[0], "max_slope") && w.size() == 2) {
            float v = 0.0f;
            if (!number(w[1], v)) { fail("max_slope: not a number"); continue; }
            rules.max_slope = std::clamp(v, 0.0f, 90.0f);
        } else if (iequals(w[0], "scale") && w.size() == 3) {
            float a = 0.0f, b = 0.0f;
            if (!number(w[1], a) || !number(w[2], b) || a <= 0.0f || b < a) { fail("scale: needs 0 < min <= max"); continue; }
            rules.min_scale = std::min(a, 100.0f);
            rules.max_scale = std::min(b, 100.0f);
        } else if (iequals(w[0], "ground") && w.size() >= 3) {
            ZoneFloraRules::Ground g;
            g.texture = std::string(w[1]);
            if (!number(w[2], g.density) || g.density < 0.0f) { fail("ground: the density is not a number >= 0"); continue; }
            g.density = std::min(g.density, 1000.0f);
            bool ok = true;
            for (std::size_t i = 3; i < w.size(); ++i) {
                ZoneFloraRules::Kind k;
                std::string_view s = w[i];
                if (iequals(s, "+green")) {
                    g.green_only = true;
                    continue;
                }
                if (auto colon = s.find(':'); colon != std::string_view::npos) {
                    if (!number(s.substr(colon + 1), k.weight) || k.weight < 0.0f) { ok = false; break; }
                    s = s.substr(0, colon);
                }
                k.definition = std::string(s);
                g.kinds.push_back(std::move(k));
            }
            if (!ok) { fail("ground: a kind's weight is not a number >= 0"); continue; }
            if (g.kinds.empty() && g.density > 0.0f) { fail("ground: no kinds"); continue; }
            rules.ground.push_back(std::move(g));
        } else {
            fail("not understood");
        }
    }
    return rules;
}

bool is_green_texel(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    const int R = r, G = g, B = b;
    return G >= 24 && G * 100 > R * 104 && G * 100 > B * 115;
}

void place_zone_flora(const Zone& zone, const ZoneFloraRules& rules, std::vector<eqg::FloraDefinition> definitions,
                      ZoneFlora& out, const TexelLookup& texel) {
    out = ZoneFlora{};
    out.definitions = std::move(definitions);
    out.tile_size = kZoneFloraTile;

    // Each ground rule's kinds as definition indices with their running weight.
    struct Pick {
        std::vector<std::pair<float, std::uint32_t>> cumulative;
        float total = 0.0f;
    };
    std::vector<Pick> picks(rules.ground.size());
    for (std::size_t g = 0; g < rules.ground.size(); ++g)
        for (const auto& k : rules.ground[g].kinds) {
            auto it = std::find_if(out.definitions.begin(), out.definitions.end(),
                                   [&](const eqg::FloraDefinition& d) { return iequals(d.name, k.definition); });
            if (it == out.definitions.end()) {
                log::debug("{}: zone flora: no definition {}", zone.name, k.definition);
                continue;
            }
            if (k.weight <= 0.0f) continue;
            picks[g].total += k.weight;
            picks[g].cumulative.emplace_back(picks[g].total,
                                             static_cast<std::uint32_t>(it - out.definitions.begin()));
        }

    const float min_up = std::cos(glm::radians(rules.max_slope));
    std::vector<FloraInstance> plants;
    std::unordered_map<std::string, int> rule_by_texture; // lower-case texture -> ground rule (-1 none)
    for (std::size_t mi = 0; mi < zone.terrain.size(); ++mi) {
        const Mesh& mesh = zone.terrain[mi];
        for (const auto& part : mesh.parts) {
            if (part.material >= mesh.materials.size()) continue;
            std::string tex = lower(texture_of(mesh.materials[part.material]));
            if (tex.empty()) continue;
            auto found = rule_by_texture.find(tex);
            if (found == rule_by_texture.end()) {
                int rule = -1;
                for (std::size_t g = 0; g < rules.ground.size() && rule < 0; ++g)
                    if (iglob(rules.ground[g].texture, tex)) rule = static_cast<int>(g);
                found = rule_by_texture.emplace(tex, rule).first;
            }
            if (found->second < 0) continue;
            const auto& ground = rules.ground[static_cast<std::size_t>(found->second)];
            const Pick& pick = picks[static_cast<std::size_t>(found->second)];
            if (ground.density <= 0.0f || pick.total <= 0.0f) continue;
            const std::uint32_t end = std::min<std::uint32_t>(part.first_index + part.index_count,
                                                              static_cast<std::uint32_t>(mesh.indices.size()));
            for (std::uint32_t i = part.first_index; i + 3 <= end; i += 3) {
                const std::uint32_t ia = mesh.indices[i], ib = mesh.indices[i + 1], ic = mesh.indices[i + 2];
                if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() || ic >= mesh.vertices.size()) continue;
                const Vertex &a = mesh.vertices[ia], &b = mesh.vertices[ib], &c = mesh.vertices[ic];
                glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
                const float len = glm::length(n);
                if (!(len > 1e-6f)) continue;
                n /= len;
                // The face's side from its vertex normals (the lit side), whatever the winding.
                if (glm::dot(n, a.normal + b.normal + c.normal) < 0.0f) n = -n;
                if (n.z < min_up) continue;
                const float area = len * 0.5f;
                MsvcRand rand{triangle_seed(mi, i / 3)};
                const float expected = area * ground.density / 100.0f;
                auto count = static_cast<std::uint32_t>(expected);
                if (unit(rand) < expected - static_cast<float>(count)) ++count;
                for (std::uint32_t k = 0; k < count; ++k) {
                    if (plants.size() >= kMaxZoneFlora) break;
                    float u = unit(rand), v = unit(rand);
                    if (u + v > 1.0f) {
                        u = 1.0f - u;
                        v = 1.0f - v;
                    }
                    const float w = 1.0f - u - v;
                    const glm::vec3 p = a.position * w + b.position * u + c.position * v;
                    // The ground's colour under the plant: the classic zones' outdoor vertices carry no colour
                    // (black), so the plant takes its ground texel's for *INHERITVERTEXCOLOR, 0x80 a neutral grey.
                    std::optional<std::uint32_t> under;
                    if (texel) under = texel(tex, a.uv * w + b.uv * u + c.uv * v);
                    if (ground.green_only && under) {
                        if (!is_green_texel(static_cast<std::uint8_t>(*under & 0xff),
                                            static_cast<std::uint8_t>(*under >> 8 & 0xff),
                                            static_cast<std::uint8_t>(*under >> 16 & 0xff))) {
                            // The rest of this plant's draws (kind, scale, width, height, turn), so the plants
                            // after it stay where they were whatever the mask keeps.
                            for (int skip = 0; skip < 5; ++skip) rand();
                            continue;
                        }
                    }
                    const float choose = unit(rand) * pick.total;
                    std::uint32_t def = pick.cumulative.back().second;
                    for (const auto& [upto, index] : pick.cumulative)
                        if (choose <= upto) {
                            def = index;
                            break;
                        }
                    const auto& d = out.definitions[def];
                    const float scale = rules.min_scale + (rules.max_scale - rules.min_scale) * unit(rand);
                    FloraInstance plant;
                    plant.position = p;
                    plant.width = (d.min_width + (d.max_width - d.min_width) * unit(rand)) * scale;
                    plant.height = (d.min_height + (d.max_height - d.min_height) * unit(rand)) * scale;
                    plant.rotation = static_cast<float>(rand() % 360);
                    plant.color = under ? 0xff000000u | (*under & 0xffffffu) : 0xff808080u;
                    plant.definition = def;
                    if (!zone.regions.empty()) {
                        const RegionInfo r = zone.regions.lookup(p + glm::vec3(0.0f, 0.0f, 1.0f));
                        if (r.is_water() || r.is_lava() || r.is_zone_line()) continue;
                    }
                    plants.push_back(plant);
                }
            }
        }
    }

    // Binned into tiles, in a stable order.
    auto key = [](const glm::vec3& p) {
        return std::pair<std::int32_t, std::int32_t>(static_cast<std::int32_t>(std::floor(p.x / kZoneFloraTile)),
                                                     static_cast<std::int32_t>(std::floor(p.y / kZoneFloraTile)));
    };
    std::map<std::pair<std::int32_t, std::int32_t>, std::vector<std::uint32_t>> tiles;
    for (std::uint32_t i = 0; i < plants.size(); ++i) tiles[key(plants[i].position)].push_back(i);
    out.instances.reserve(plants.size());
    for (const auto& [k, list] : tiles) {
        ZoneFlora::Tile t;
        t.min = glm::vec2(static_cast<float>(k.first), static_cast<float>(k.second)) * kZoneFloraTile;
        t.first = static_cast<std::uint32_t>(out.instances.size());
        t.count = static_cast<std::uint32_t>(list.size());
        for (auto i : list) out.instances.push_back(plants[i]);
        out.tiles.push_back(t);
    }
}

std::size_t add_zone_flora(const GameDir& game, Zone& zone, const std::filesystem::path& rules_dir) {
    if (!zone.flora.empty()) return 0;
    const auto path = rules_dir / (lower(zone.name) + ".txt");
    std::ifstream in(path, std::ios::binary);
    if (!in) return 0;
    std::stringstream ss;
    ss << in.rdbuf();
    std::vector<std::string> errors;
    ZoneFloraRules rules = parse_zone_flora_rules(ss.str(), &errors);
    for (const auto& e : errors) log::warn("{}: {}", path.string(), e);
    if (rules.donor.empty()) {
        log::warn("{}: no donor archive", path.string());
        return 0;
    }
    auto found = game.find(rules.donor);
    if (!found) {
        log::info("{}: zone flora: the donor {} is not in the game folder", zone.name, rules.donor);
        return 0;
    }
    std::shared_ptr<const PfsArchive> donor;
    try {
        donor = std::make_shared<const PfsArchive>(PfsArchive::open(*found));
    } catch (const std::exception& e) {
        log::warn("{}: zone flora: {}: {}", zone.name, rules.donor, e.what());
        return 0;
    }
    auto rfd = donor->read("radialfloradefs.rfd");
    if (!rfd) {
        log::info("{}: zone flora: {} has no radialfloradefs.rfd", zone.name, rules.donor);
        return 0;
    }
    // +green reads the game's own ground textures (not a texture pack's), each decoded once.
    std::unordered_map<std::string, std::optional<Image>> decoded;
    TexelLookup texel = [&](std::string_view name, glm::vec2 uv) -> std::optional<std::uint32_t> {
        auto it = decoded.find(std::string(name));
        if (it == decoded.end()) {
            std::optional<Image> image;
            if (auto data = zone.read_file(name)) image = decode_texture(*data, {.wld_bmp_rows = true});
            if (image && (image->format != PixelFormat::RGBA8 || image->mips.empty() || image->width == 0 ||
                          image->height == 0 || image->mips[0].size() < std::size_t{image->width} * image->height * 4))
                image.reset();
            it = decoded.emplace(std::string(name), std::move(image)).first;
        }
        if (!it->second) return std::nullopt;
        const Image& im = *it->second;
        auto wrap = [](float f, std::uint32_t n) {
            float t = f - std::floor(f);
            return std::min(static_cast<std::uint32_t>(t * static_cast<float>(n)), n - 1);
        };
        const std::size_t at = (std::size_t{wrap(uv.y, im.height)} * im.width + wrap(uv.x, im.width)) * 4;
        const auto& px = im.mips[0];
        return static_cast<std::uint32_t>(px[at]) | static_cast<std::uint32_t>(px[at + 1]) << 8 |
               static_cast<std::uint32_t>(px[at + 2]) << 16;
    };
    place_zone_flora(zone, rules, eqg::parse_radial_flora(*rfd), zone.flora, texel);
    zone.flora.textures = donor;
    log::info("{}: zone flora: {} plants on {} tiles, from {}", zone.name, zone.flora.instances.size(),
              zone.flora.tiles.size(), rules.donor);
    return zone.flora.instances.size();
}

} // namespace nmsc::assets
