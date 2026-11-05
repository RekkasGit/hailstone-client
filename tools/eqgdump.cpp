// eqgdump ARCHIVE [MEMBER]      list the EQG models/zone files in an archive, or dump one
// eqgdump --shaders ARCHIVE...  shader names and material properties over many archives
// eqgdump --zone NAME [NAME...] load zones (EQG or classic, as the client picks) and
//                               report counts; "--zone all" loads every EQG zone
// EQGDUMP_REGIONS=1 with --zone: region tags, boxes and the buried zone lines.
// Needs EQ_GAME_DIR (or --game-dir DIR first) for --zone.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "assets/eqg.h"
#include "assets/pfs.h"
#include "assets/zone.h"
#include "core/game_dir.h"
#include "core/log.h"
#include "game/collision.h"
#include "game/zone_lines.h"

using namespace nmsc;
using namespace nmsc::assets;

namespace {

bool ends_with(const std::string& s, const char* suffix) {
    std::size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

void dump_model(const eqg::ModelFile& m) {
    std::printf("  %s v%u: %zu materials, %zu vertices, %zu triangles, %u bones\n", m.terrain ? "TER" : "MOD",
                m.version, m.materials.size(), m.positions.size(), m.triangles.size(), m.bone_count);
    for (const auto& mat : m.materials) {
        std::printf("    [%u] %s  %s", mat.id, mat.name.c_str(), mat.shader.c_str());
        for (const auto& p : mat.properties) {
            if (p.type == 2) std::printf("  %s=%s", p.name.c_str(), p.text.c_str());
            else if (p.type == 0) std::printf("  %s=%g", p.name.c_str(), static_cast<double>(p.f));
            else std::printf("  %s=0x%x", p.name.c_str(), p.u);
        }
        std::printf("\n");
    }
    std::map<std::uint32_t, std::size_t> flags;
    for (const auto& t : m.triangles) ++flags[t.flags];
    std::printf("    triangle flags:");
    for (auto [f, n] : flags) std::printf(" 0x%x×%zu", f, n);
    std::printf("\n");
}

void dump_zon(const eqg::ZonFile& z) {
    std::printf("  ZON v%u: %zu models, %zu placements, %zu regions, %zu lights\n", z.version, z.models.size(),
                z.placements.size(), z.regions.size(), z.lights.size());
    for (std::size_t i = 0; i < z.models.size() && i < 8; ++i) std::printf("    model %zu: %s\n", i, z.models[i].c_str());
    for (std::size_t i = 0; i < z.placements.size() && i < 12; ++i) {
        const auto& p = z.placements[i];
        std::printf("    place %d %s pos (%.2f %.2f %.2f) rot (%.4f %.4f %.4f) scale %.3f lit %zu\n", p.model,
                    p.name.c_str(), p.position.x, p.position.y, p.position.z, p.rotation.x, p.rotation.y,
                    p.rotation.z, p.scale, p.lighting.size());
    }
    for (const auto& g : z.regions)
        std::printf("    region %s centre (%.1f %.1f %.1f) angles (%.4f %.4f %.4f) ext (%.1f %.1f %.1f)\n",
                    g.name.c_str(), g.center.x, g.center.y, g.center.z, g.angles.x, g.angles.y, g.angles.z,
                    g.extents.x, g.extents.y, g.extents.z);
    for (std::size_t i = 0; i < z.lights.size() && i < 6; ++i) {
        const auto& l = z.lights[i];
        std::printf("    light %s (%.1f %.1f %.1f) colour (%.2f %.2f %.2f) r %.1f\n", l.name.c_str(), l.position.x,
                    l.position.y, l.position.z, l.color.x, l.color.y, l.color.z, l.radius);
    }
}

int dump_archive(int argc, char** argv) {
    auto archive = PfsArchive::open(argv[1]);
    if (argc == 2) {
        for (const auto& name : archive.names()) {
            if (!(ends_with(name, ".ter") || ends_with(name, ".mod") || ends_with(name, ".zon"))) continue;
            auto data = archive.read(name);
            std::printf("%s (%zu bytes)\n", name.c_str(), data ? data->size() : 0);
            try {
                if (ends_with(name, ".zon")) {
                    if (eqg::is_heightmap_zon(*data)) {
                        auto o = eqg::parse_heightmap_zon(*data);
                        std::printf("  EQTZP %s: lng %d..%d lat %d..%d, %u quads × %.1f units\n", o.name.c_str(),
                                    o.min_lng, o.max_lng, o.min_lat, o.max_lat, o.quads_per_tile,
                                    static_cast<double>(o.units_per_vertex));
                    } else {
                        auto z = eqg::parse_zon(*data);
                        std::printf("  ZON v%u: %zu models, %zu placements, %zu regions, %zu lights\n", z.version,
                                    z.models.size(), z.placements.size(), z.regions.size(), z.lights.size());
                    }
                } else {
                    auto m = eqg::parse_model(*data, ends_with(name, ".ter"));
                    std::printf("  v%u: %zu materials, %zu vertices, %zu triangles\n", m.version, m.materials.size(),
                                m.positions.size(), m.triangles.size());
                }
            } catch (const std::exception& e) {
                std::printf("  error: %s\n", e.what());
            }
        }
        return 0;
    }
    std::string member = argv[2];
    auto data = archive.read(member);
    if (!data) {
        // A loose file next to the archive (many .zon files live in the install root).
        auto loose = read_file(std::filesystem::path(argv[1]).parent_path() / member);
        if (!loose) {
            std::fprintf(stderr, "%s: no file %s\n", argv[1], argv[2]);
            return 1;
        }
        data = std::move(loose);
    }
    if (ends_with(member, ".zon")) dump_zon(eqg::parse_zon(*data));
    else dump_model(eqg::parse_model(*data, ends_with(member, ".ter")));
    return 0;
}

int survey_shaders(int argc, char** argv) {
    std::map<std::string, std::size_t> shaders;
    std::map<std::string, std::size_t> props;
    std::map<std::string, std::size_t> versions;
    for (int i = 2; i < argc; ++i) {
        try {
            auto archive = PfsArchive::open(argv[i]);
            for (const auto& name : archive.names()) {
                bool ter = ends_with(name, ".ter");
                if (!ter && !ends_with(name, ".mod")) continue;
                auto data = archive.read(name);
                try {
                    auto m = eqg::parse_model(*data, ter);
                    ++versions[std::string(ter ? "ter v" : "mod v") + std::to_string(m.version)];
                    for (const auto& mat : m.materials) {
                        ++shaders[mat.shader];
                        for (const auto& p : mat.properties) ++props[p.name + " (type " + std::to_string(p.type) + ")"];
                    }
                } catch (const std::exception& e) {
                    ++versions[std::string("error: ") + e.what()];
                }
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s: %s\n", argv[i], e.what());
        }
    }
    for (auto& [k, n] : versions) std::printf("%8zu  %s\n", n, k.c_str());
    std::printf("shaders:\n");
    for (auto& [k, n] : shaders) std::printf("%8zu  %s\n", n, k.c_str());
    std::printf("properties:\n");
    for (auto& [k, n] : props) std::printf("%8zu  %s\n", n, k.c_str());
    return 0;
}

int load_zones(const GameDir& game, std::vector<std::string> names) {
    if (names.size() == 1 && names[0] == "all") {
        names.clear();
        std::set<std::string> found;
        for (const auto& entry : std::filesystem::directory_iterator(game.root())) {
            auto file = entry.path().filename().string();
            for (char& c : file) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (!ends_with(file, ".eqg")) continue;
            std::string zone = file.substr(0, file.size() - 4);
            // A zone archive carries a terrain or a zone declaration.
            try {
                auto a = PfsArchive::open(entry.path());
                bool zoneish = game.exists(zone + ".zon");
                for (const auto& n : a.names()) zoneish |= ends_with(n, ".ter") || ends_with(n, ".zon");
                if (zoneish) found.insert(zone);
            } catch (const std::exception&) {
            }
        }
        names.assign(found.begin(), found.end());
    }
    int failed = 0;
    for (const auto& name : names) {
        auto t0 = std::chrono::steady_clock::now();
        try {
            auto zone = load_zone(game, name);
            if (std::getenv("EQGDUMP_REGIONS"))
                for (const auto& r : zone.regions.tagged()) {
                    std::printf("  region %u %s flags 0x%08x%s\n", r.region, r.tag.c_str(), r.flags,
                                r.is_zone_line() ? " (zone line)" : "");
                    auto want = [&](std::uint32_t n) { return n == r.region; };
                    for (const auto& c : zone.regions.leaf_cells(glm::vec3(-1e5f), glm::vec3(1e5f), want))
                        std::printf("    box (%.1f %.1f %.1f)..(%.1f %.1f %.1f)\n", c.lo.x, c.lo.y, c.lo.z, c.hi.x,
                                    c.hi.y, c.hi.z);
                }
            if (std::getenv("EQGDUMP_REGIONS") && zone.regions.has_boxes()) {
                // The buried zone-line fallback the client applies (game/zone_lines.h).
                auto collision = game::CollisionMesh::build(zone);
                for (const auto& b : game::survey_zone_lines(zone.regions, collision))
                    std::printf("  %s zone line %s (box %u): box z %.1f..%.1f, ground %.1f..%.1f, samples %d covered %d "
                                "inside %d reachable %d shallow %d%s\n",
                                b.buried ? "buried" : "kept", b.tag.c_str(), b.region, b.box_lo_z, b.box_hi_z,
                                b.covered ? b.ground_lo : 0.0f, b.covered ? b.ground_hi : 0.0f, b.samples, b.covered,
                                b.inside, b.reachable, b.shallow,
                                b.buried ? std::format(", fires up to z {:.1f}", b.top_z).c_str() : "");
            }
            if (const char* out = std::getenv("EQGDUMP_VERTS")) {
                // World-space vertices of terrain and placed objects, as f32 triples (for map comparisons).
                std::FILE* f = std::fopen(out, "wb");
                auto put = [&](glm::vec3 p) { std::fwrite(&p, sizeof p, 1, f); };
                for (const auto& m : zone.terrain)
                    for (const auto& v : m.vertices) put(v.position);
                for (const auto& p : zone.placements)
                    if (auto it = zone.objects.find(p.actor); it != zone.objects.end())
                        for (const auto& v : it->second.vertices) put(glm::vec3(p.transform * glm::vec4(v.position, 1.0f)));
                std::fclose(f);
            }
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
            std::size_t placed = 0, placed_vertices = 0;
            for (const auto& p : zone.placements)
                if (auto it = zone.objects.find(p.actor); it != zone.objects.end()) {
                    placed += it->second.indices.size() / 3;
                    placed_vertices += it->second.vertices.size();
                }
            std::size_t tris = 0;
            glm::vec3 lo(1e30f), hi(-1e30f);
            for (const auto& m : zone.terrain) {
                tris += m.indices.size() / 3;
                for (const auto& v : m.vertices) {
                    lo = glm::min(lo, v.position);
                    hi = glm::max(hi, v.position);
                }
            }
            std::printf("ok   %-22s %s terrain %zu meshes %zu tris, %zu objects, %zu placements (%zu tris, %zu vertices), %zu lights, %zu regions, "
                        "bounds (%.0f %.0f %.0f)..(%.0f %.0f %.0f), terrain (%.0f %.0f %.0f)..(%.0f %.0f %.0f), %lld ms\n",
                        name.c_str(), is_eqg_zone(game, name) ? "eqg" : "s3d", zone.terrain.size(), tris,
                        zone.objects.size(), zone.placements.size(), placed, placed_vertices, zone.lights.size(), zone.regions.region_count(),
                        zone.bounds_min.x, zone.bounds_min.y, zone.bounds_min.z, zone.bounds_max.x, zone.bounds_max.y,
                        zone.bounds_max.z, lo.x, lo.y, lo.z, hi.x, hi.y, hi.z, static_cast<long long>(ms.count()));
        } catch (const std::exception& e) {
            ++failed;
            std::printf("FAIL %-22s %s\n", name.c_str(), e.what());
        }
        std::fflush(stdout);
    }
    std::printf("%zu zones, %d failed\n", names.size(), failed);
    return failed ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string game_dir;
    if (argc > 2 && !std::strcmp(argv[1], "--game-dir")) {
        game_dir = argv[2];
        argv += 2;
        argc -= 2;
    } else if (const char* env = std::getenv("EQ_GAME_DIR")) {
        game_dir = env;
    }
    if (argc < 2) {
        std::fprintf(stderr, "usage: eqgdump ARCHIVE [MEMBER] | --shaders ARCHIVE... | --zone NAME...|all\n");
        return 2;
    }
    try {
        if (!std::strcmp(argv[1], "--shaders")) return survey_shaders(argc, argv);
        if (!std::strcmp(argv[1], "--zone")) {
            if (game_dir.empty()) throw std::runtime_error("--zone needs EQ_GAME_DIR or --game-dir");
            if (std::getenv("NMSC_DEBUG")) log::set_level(log::Level::Debug);
            GameDir game(game_dir);
            return load_zones(game, std::vector<std::string>(argv + 2, argv + argc));
        }
        return dump_archive(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "eqgdump: %s\n", e.what());
        return 1;
    }
}
