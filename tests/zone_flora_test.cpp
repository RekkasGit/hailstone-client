// Hailstone's flora for zones without any (assets/zone_flora.h): the rules file and the placement over the terrain.
#include <doctest/doctest.h>

#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "assets/flora.h"
#include "assets/zone.h"
#include "assets/zone_flora.h"
#include "game_files.h"

using namespace nmsc;
using namespace nmsc::assets;

namespace {

eqg::FloraDefinition definition(const std::string& name, float size) {
    eqg::FloraDefinition d;
    d.name = name;
    d.texture = name + ".dds";
    d.min_width = d.max_width = size;
    d.min_height = d.max_height = size;
    return d;
}

// A square of side `size` at height `z`, its far corner raised by `rise` (a slope), facing up (or down when `flip`),
// textured `texture`.
Mesh square(const std::string& texture, float size, float rise = 0.0f, bool flip = false) {
    Mesh m;
    glm::vec3 up = flip ? glm::vec3(0, 0, -1) : glm::vec3(0, 0, 1);
    for (auto p : {glm::vec3(0, 0, 0), glm::vec3(size, 0, 0), glm::vec3(size, size, rise), glm::vec3(0, size, rise)}) {
        Vertex v;
        v.position = p;
        v.normal = up;
        v.color = 0xff808080u;
        m.vertices.push_back(v);
    }
    m.indices = {0, 1, 2, 0, 2, 3};
    Material mat;
    mat.frames = {texture};
    m.materials.push_back(mat);
    m.parts.push_back({0, 6, 0, true});
    return m;
}

} // namespace

TEST_CASE("zone flora: the rules file") {
    std::vector<std::string> errors;
    auto rules = parse_zone_flora_rules(R"(# East Commonlands
donor Commonlands.eqg
max_slope 30
scale 0.8 1.2
ground cpath* 0          # the roads stay bare
ground CPG* 12.5 grassblades:3 grass2
ground sand 2 DesertPlant1:0.5
ground bad x grass
ground bad 1 grass:-1
nonsense here
)", &errors);
    CHECK(rules.donor == "commonlands.eqg");
    CHECK(rules.max_slope == 30.0f);
    CHECK(rules.min_scale == doctest::Approx(0.8f));
    CHECK(rules.max_scale == doctest::Approx(1.2f));
    REQUIRE(rules.ground.size() == 3);
    CHECK(rules.ground[0].texture == "cpath*");
    CHECK(rules.ground[0].density == 0.0f);
    CHECK(rules.ground[1].density == 12.5f);
    REQUIRE(rules.ground[1].kinds.size() == 2);
    CHECK(rules.ground[1].kinds[0].definition == "grassblades");
    CHECK(rules.ground[1].kinds[0].weight == 3.0f);
    CHECK(rules.ground[1].kinds[1].weight == 1.0f);
    CHECK(rules.ground[2].kinds[0].weight == 0.5f);
    CHECK(errors.size() == 3);
}

TEST_CASE("zone flora: placement by texture, density and slope") {
    ZoneFloraRules rules = parse_zone_flora_rules("donor x.eqg\nmax_slope 30\nground path* 0\n"
                                                  "ground grass* 10 tall:1 short:1\n");
    std::vector<eqg::FloraDefinition> defs = {definition("short", 2.0f), definition("tall", 5.0f)};
    Zone zone;
    zone.name = "test";
    zone.terrain.push_back(square("GRASS01.BMP", 100.0f));         // 10,000 units: about 1,000 plants
    zone.terrain.push_back(square("path1.bmp", 100.0f));           // bare
    zone.terrain.push_back(square("rock.bmp", 100.0f));            // no rule
    zone.terrain.push_back(square("grass02.bmp", 100.0f, 100.0f)); // 45 degrees: too steep
    zone.terrain.push_back(square("grass03.bmp", 100.0f, 0.0f, true)); // a ceiling
    ZoneFlora flora;
    place_zone_flora(zone, rules, defs, flora);
    CHECK(flora.definitions.size() == 2);
    CHECK(flora.instances.size() > 900);
    CHECK(flora.instances.size() < 1100);
    std::size_t tall = 0;
    for (const auto& p : flora.instances) {
        CHECK(p.position.x >= 0.0f);
        CHECK(p.position.x <= 100.0f);
        CHECK(p.position.z == 0.0f);
        CHECK(p.color == 0xff808080u); // no texel lookup: neutral
        if (p.definition == 1) {
            ++tall;
            CHECK(p.height == 5.0f);
        }
    }
    CHECK(tall > 400);
    CHECK(tall < 600);
    // Binned by kZoneFloraTile: the 100-unit square lies in one tile.
    REQUIRE(flora.tiles.size() == 1);
    CHECK(flora.tiles[0].count == flora.instances.size());
    CHECK(flora.tile_size == kZoneFloraTile);

    // The same plants at every load.
    ZoneFlora again;
    place_zone_flora(zone, rules, defs, again);
    REQUIRE(again.instances.size() == flora.instances.size());
    CHECK(again.instances.front().position == flora.instances.front().position);
    CHECK(again.instances.back().rotation == flora.instances.back().rotation);

    // A gentler limit lets the slope grow too; a kind the definitions lack places nothing.
    rules.max_slope = 50.0f;
    ZoneFlora steep;
    place_zone_flora(zone, rules, defs, steep);
    CHECK(steep.instances.size() > flora.instances.size() + 900);
    ZoneFlora none;
    place_zone_flora(zone, parse_zone_flora_rules("ground grass* 10 missing\n"), defs, none);
    CHECK(none.instances.empty());
}

TEST_CASE("zone flora: +green keeps the plants on the grass of a blended texture") {
    CHECK(is_green_texel(60, 90, 40));
    CHECK_FALSE(is_green_texel(200, 170, 120)); // sand
    CHECK_FALSE(is_green_texel(150, 110, 70));  // cliff
    CHECK_FALSE(is_green_texel(10, 14, 8));     // too dark to tell
    CHECK_FALSE(is_green_texel(80, 80, 80));    // grey
    std::vector<eqg::FloraDefinition> defs = {definition("grass", 2.0f)};
    Zone zone;
    zone.terrain.push_back(square("blend.bmp", 100.0f));
    for (auto& v : zone.terrain[0].vertices) v.uv = glm::vec2(v.position) / 100.0f;
    // The picture's left half is grass, its right half sand.
    TexelLookup texel = [](std::string_view, glm::vec2 uv) -> std::optional<std::uint32_t> {
        return uv.x - std::floor(uv.x) < 0.5f ? 0x285a3cu : 0x78aac8u;
    };
    ZoneFlora all, masked, unreadable;
    place_zone_flora(zone, parse_zone_flora_rules("ground blend* 10 grass\n"), defs, all, texel);
    place_zone_flora(zone, parse_zone_flora_rules("ground blend* 10 +green grass\n"), defs, masked, texel);
    CHECK(all.instances.size() > 900);
    CHECK(masked.instances.size() > 400);
    CHECK(masked.instances.size() < 600);
    for (const auto& p : masked.instances) {
        CHECK(p.position.x < 50.0f);
        CHECK(p.color == 0xff285a3cu); // the ground's texel
    }
    // A texture the lookup cannot read keeps every spot.
    place_zone_flora(zone, parse_zone_flora_rules("ground blend* 10 +green grass\n"), defs, unreadable,
                     [](std::string_view, glm::vec2) { return std::optional<std::uint32_t>(); });
    CHECK(unreadable.instances.size() == all.instances.size());
}

TEST_CASE("zone flora: East Commonlands' ground (game files)") {
    const GameDir* game = test::game_dir();
    if (!game || !game->exists("ecommons.s3d")) return;
    Zone zone = load_zone(*game, "ecommons");
    // The terrain's area by texture and how much of it faces up, for tuning flora/ecommons.txt.
    struct Stat {
        double area = 0.0, flat = 0.0;
        double r = 0, g = 0, b = 0;
    };
    std::map<std::string, Stat> stats;
    for (const auto& mesh : zone.terrain)
        for (const auto& part : mesh.parts) {
            if (part.material >= mesh.materials.size() || mesh.materials[part.material].frames.empty()) continue;
            auto& s = stats[mesh.materials[part.material].frames.front()];
            for (std::uint32_t i = part.first_index; i + 3 <= part.first_index + part.index_count; i += 3) {
                const auto &a = mesh.vertices[mesh.indices[i]], &b = mesh.vertices[mesh.indices[i + 1]],
                           &c = mesh.vertices[mesh.indices[i + 2]];
                glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
                double area = glm::length(n) * 0.5;
                if (glm::dot(n, a.normal + b.normal + c.normal) < 0.0f) n = -n;
                s.area += area;
                if (glm::length(n) > 0 && glm::normalize(n).z > 0.766f) s.flat += area;
                s.r += area * (a.color & 0xff);
                s.g += area * (a.color >> 8 & 0xff);
                s.b += area * (a.color >> 16 & 0xff);
            }
        }
    for (const auto& [tex, s] : stats)
        if (s.area > 0)
            MESSAGE(tex << " area " << static_cast<long long>(s.area) << " flat " << static_cast<long long>(s.flat)
                        << " colour " << static_cast<int>(s.r / s.area) << "," << static_cast<int>(s.g / s.area) << ","
                        << static_cast<int>(s.b / s.area));
    CHECK(!stats.empty());

    // The shipped rules: plants on the meadows, none in the water.
    REQUIRE(add_zone_flora(*game, zone, NMSC_SOURCE_DIR "/flora") > 50000);
    CHECK(zone.flora.textures);
    std::size_t wet = 0;
    for (const auto& p : zone.flora.instances)
        if (zone.regions.lookup(p.position + glm::vec3(0, 0, 1)).is_water()) ++wet;
    CHECK(wet == 0);
    for (std::size_t i = 0; i < zone.flora.instances.size(); i += zone.flora.instances.size() / 8) {
        const auto& p = zone.flora.instances[i];
        MESSAGE("plant " << p.position.x << " " << p.position.y << " " << p.position.z << " colour " << std::hex
                         << p.color << std::dec);
    }
}
