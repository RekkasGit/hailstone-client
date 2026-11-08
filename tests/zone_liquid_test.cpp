#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "assets/regions.h"
#include "assets/wld.h"
#include "assets/zone.h"
#include "render/zone_renderer.h"

using namespace hailstone;
namespace wld = hailstone::assets::wld;

namespace {

wld::BspNode split(glm::vec3 n, float d, std::uint32_t front, std::uint32_t back) {
    wld::BspNode node;
    node.normal = n;
    node.distance = d;
    node.front = front;
    node.back = back;
    return node;
}

wld::BspNode leaf(std::uint32_t region) {
    wld::BspNode node;
    node.region = region;
    return node;
}

wld::RegionTag tag(std::string text, std::vector<std::uint32_t> regions) {
    wld::RegionTag t;
    t.tag = std::move(text);
    t.regions = std::move(regions);
    return t;
}

} // namespace

TEST_CASE("classic liquids: passable surfaces on a liquid region's top, carried through a dry zone line") {
    // North Ro's shore in miniature: under z 0 the sea (region 1, "WT") for x <= 100, a zone line's dry region
    // (region 2, "DR..TP") for 100 < x <= 120, plain ground (region 3) beyond and above.
    std::vector<wld::BspNode> nodes = {
        split({0, 0, -1}, 0, 2, 6),    // 1: z <= 0
        split({-1, 0, 0}, 100, 4, 3),  // 2: x <= 100
        split({-1, 0, 0}, 120, 5, 6),  // 3: x <= 120
        leaf(1),                       // 4
        leaf(2),                       // 5
        leaf(3),                       // 6
    };
    assets::Zone zone;
    zone.regions = assets::ZoneRegions::build(
        nodes, 3, {tag("WT_ZONE", {0}), tag("DRNTP00255000002000000000000000_ZONE", {1})});

    assets::Mesh m;
    auto tri = [&](float x0, float x1, float z0, float z1) {
        for (glm::vec3 p : {glm::vec3(x0, 0, z0), glm::vec3(x0, 10, z0), glm::vec3(x1, 0, z1)}) {
            assets::Vertex v;
            v.position = p;
            m.indices.push_back(static_cast<std::uint32_t>(m.vertices.size()));
            m.vertices.push_back(v);
        }
    };
    // The water material, passable (part 0): triangles 0..3.
    tri(10, 20, -0.5f, -0.5f);   // 0: on the sea's top
    tri(105, 115, -0.5f, -0.5f); // 1: the same surface over the zone line's dry region
    tri(105, 115, -10, -10);     // 2: the same material lower down, in the dry region: not the surface
    tri(30, 40, -0.5f, 9.5f);    // 3: sloped
    // Solid sand at the waterline (part 1): triangle 4.
    tri(50, 60, -0.5f, -0.5f);
    m.materials.resize(2);
    m.materials[0].name = "W1_MDF";
    m.materials[0].frames = {"w1.bmp"};
    m.materials[1].name = "SFS1_MDF";
    m.materials[1].frames = {"sfs1.bmp"};
    m.parts.push_back({0, 12, 0, false});
    m.parts.push_back({12, 3, 1, true});
    zone.terrain.push_back(m);

    const auto liquid = render::classic_liquid_triangles(zone);
    REQUIRE(liquid.size() == 1);
    REQUIRE(liquid[0].size() == 5);
    CHECK(liquid[0][0] == render::kLiquidWater);
    CHECK(liquid[0][1] == render::kLiquidWater);
    CHECK(liquid[0][2] == 0);
    CHECK(liquid[0][3] == 0);
    CHECK(liquid[0][4] == 0); // solid: the beach, not the sea

    // A passable sand triangle there would be the sea's.
    zone.terrain[0].parts[1].solid = false;
    CHECK(render::classic_liquid_triangles(zone)[0][4] == render::kLiquidWater);
}

TEST_CASE("zone renderer: a prepare that is cancelled stops and returns nothing") {
    // A neighbour zone's load thrown away on zoning (app/neighbour_zones.cpp) asks between placements and materials.
    assets::Zone zone;
    zone.placements.resize(3);
    render::ZoneRenderOptions options;
    options.cancelled = [] { return true; };
    CHECK_FALSE(render::ZoneRenderer::prepare(zone, options, 16));
    int asked = 0;
    options.cancelled = [&asked] {
        ++asked;
        return false;
    };
    CHECK(render::ZoneRenderer::prepare(zone, options, 16));
    CHECK(asked > 0);
}
