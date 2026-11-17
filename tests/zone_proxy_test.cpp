// render/zone_proxy.h: the distant copies of the zones round the current one, baked from their files.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "core/byte_io.h"
#include "render/horizon.h"
#include "render/zone_proxy.h"

using namespace hailstone;

namespace {

// A 2x1 24-bit BMP, both texels the one colour.
std::vector<std::byte> bmp_of(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    ByteWriter w;
    w.le<std::uint8_t>('B');
    w.le<std::uint8_t>('M');
    w.le<std::uint32_t>(14 + 40 + 8);
    w.le<std::uint32_t>(0);
    w.le<std::uint32_t>(14 + 40);
    w.le<std::uint32_t>(40);
    w.le<std::int32_t>(2);
    w.le<std::int32_t>(1);
    w.le<std::uint16_t>(1);
    w.le<std::uint16_t>(24);
    w.le<std::uint32_t>(0);
    w.le<std::uint32_t>(8);
    w.zeros(16);
    for (int k = 0; k < 2; ++k)
        for (std::uint8_t v : {b, g, r}) w.le(v);
    w.zeros(2);
    return w.take();
}

struct Builder {
    assets::Mesh mesh;
    // A level quad x0..x1, y0..y1 at z with material m.
    void level(float x0, float y0, float x1, float y1, float z, std::uint32_t m) {
        quad({x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}, m);
    }
    void quad(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, std::uint32_t m) {
        const auto first = static_cast<std::uint32_t>(mesh.indices.size());
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const glm::vec3& p : {a, b, c, d}) {
            assets::Vertex v;
            v.position = p;
            v.color = 0xff102030u;
            mesh.vertices.push_back(v);
        }
        for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(base + i);
        mesh.parts.push_back({first, 6, m});
    }
};

// A zone 2048 units square: red ground at 0; a house (400..600, green roof at 80, blue walls); a ridge 500 high
// ringing its west edge (x < 192); a city wall 100 high and 64 wide inside the ring (x 200..264); and, cut off past a
// gap, a scrap of ground (x 1700..1740) over a hole from x 1500 on. A tree stands at (1000, 1000).
assets::Zone test_zone() {
    assets::Zone zone;
    zone.name = "testzone";
    zone.bounds_min = {0.0f, 0.0f, -10.0f};
    zone.bounds_max = {2048.0f, 2048.0f, 600.0f};
    zone.generated_files["red.bmp"] = bmp_of(255, 0, 0);
    zone.generated_files["green.bmp"] = bmp_of(0, 255, 0);
    zone.generated_files["blue.bmp"] = bmp_of(0, 0, 255);
    zone.generated_files["grey.bmp"] = bmp_of(128, 128, 128);
    zone.generated_files["leaves.bmp"] = bmp_of(0, 120, 0);
    Builder b;
    b.mesh.materials.resize(4);
    b.mesh.materials[0].frames = {"red.bmp"};
    b.mesh.materials[1].frames = {"green.bmp"};
    b.mesh.materials[2].frames = {"blue.bmp"};
    b.mesh.materials[3].frames = {"grey.bmp"};
    b.level(0.0f, 0.0f, 1500.0f, 2048.0f, 0.0f, 0);
    b.level(1700.0f, 1000.0f, 1740.0f, 1040.0f, 0.0f, 0);
    b.level(400.0f, 400.0f, 600.0f, 600.0f, 80.0f, 1);
    b.quad({400.0f, 400.0f, 0.0f}, {400.0f, 600.0f, 0.0f}, {400.0f, 600.0f, 80.0f}, {400.0f, 400.0f, 80.0f}, 2);
    b.level(0.0f, 0.0f, 192.0f, 2048.0f, 500.0f, 3);
    b.level(200.0f, 0.0f, 264.0f, 2048.0f, 100.0f, 3);
    zone.terrain.push_back(b.mesh);
    // The tree: a masked card 40 high named as a tree.
    Builder t;
    t.mesh.materials.resize(1);
    t.mesh.materials[0].frames = {"leaves.bmp"};
    t.mesh.materials[0].blend = assets::BlendMode::Masked;
    t.mesh.materials[0].name = "tree_leaves";
    t.quad({-10.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 40.0f}, {-10.0f, 0.0f, 40.0f}, 0);
    t.quad({0.0f, -10.0f, 0.0f}, {0.0f, 10.0f, 0.0f}, {0.0f, 10.0f, 40.0f}, {0.0f, -10.0f, 40.0f}, 0);
    zone.objects["TREE1_ACTORDEF"] = t.mesh;
    assets::Placement p;
    p.actor = "TREE1_ACTORDEF";
    p.transform = glm::translate(glm::mat4(1.0f), glm::vec3(1000.0f, 1000.0f, 0.0f));
    zone.placements.push_back(p);
    return zone;
}

std::size_t cell_at(const render::ZoneProxy& z, float x, float y) {
    const int cx = static_cast<int>((x - z.lo.x) / z.cell), cy = static_cast<int>((y - z.lo.y) / z.cell);
    return static_cast<std::size_t>(cy) * static_cast<std::size_t>(z.nx) + static_cast<std::size_t>(cx);
}

glm::ivec3 texel(const assets::Image& image, std::uint32_t x, std::uint32_t y) {
    const std::size_t i = (static_cast<std::size_t>(y) * image.width + x) * 4;
    return {std::to_integer<int>(image.mips[0][i]), std::to_integer<int>(image.mips[0][i + 1]),
            std::to_integer<int>(image.mips[0][i + 2])};
}

} // namespace

TEST_CASE("zone proxy: the bake's heights, colour, walls' colour and trees; the ring's ridge and a scrap go") {
    const auto z = render::bake_zone_proxy(test_zone(), 77);
    REQUIRE(z.valid());
    CHECK(z.key == 77);
    CHECK(z.cell == doctest::Approx(render::kProxyCell));
    CHECK(z.nx == 128);
    CHECK(z.ny == 128);
    CHECK(z.light == 0xff102030u);
    // The ground, the roof.
    CHECK(z.height[cell_at(z, 1000.0f, 1300.0f)] == doctest::Approx(0.0f));
    CHECK(z.drawn[cell_at(z, 1000.0f, 1300.0f)]);
    CHECK(z.height[cell_at(z, 500.0f, 500.0f)] == doctest::Approx(80.0f));
    // The ridge ringing the edge goes; the city's own wall inside the ring stays.
    CHECK_FALSE(z.drawn[cell_at(z, 100.0f, 1000.0f)]);
    CHECK(z.height[cell_at(z, 232.0f, 1000.0f)] == doctest::Approx(100.0f));
    CHECK(z.drawn[cell_at(z, 232.0f, 1000.0f)]);
    // The scrap past the hole is cut off from the rest, and too small: it goes. The hole has nothing.
    CHECK_FALSE(z.drawn[cell_at(z, 1720.0f, 1020.0f)]);
    CHECK(std::isnan(z.height[cell_at(z, 1600.0f, 1020.0f)]));
    // The tree is no part of the surface: a picture where it stands.
    CHECK(z.height[cell_at(z, 1000.0f, 1000.0f)] == doctest::Approx(0.0f));
    REQUIRE(z.trees.size() == 1);
    REQUIRE(z.tree_kinds.size() == 1);
    CHECK(glm::vec3(z.trees[0].transform[3]) == glm::vec3(1000.0f, 1000.0f, 0.0f));
    // The picture: green over the roof, red over the ground; the walls' strip blue by the house's wall.
    CHECK(z.picture.height == z.top_height + render::kProxyStripGap + static_cast<std::uint32_t>(z.ny));
    const auto roof = texel(z.picture, static_cast<std::uint32_t>(500.0f / z.texel), static_cast<std::uint32_t>(500.0f / z.texel));
    CHECK(roof.y > 200);
    const auto ground = texel(z.picture, static_cast<std::uint32_t>(1000.0f / z.texel), static_cast<std::uint32_t>(1300.0f / z.texel));
    CHECK(ground.x > 200);
    const auto wall = texel(z.picture, 25, z.top_height + render::kProxyStripGap + 31); // the cell at (400, 500)
    CHECK(wall.z > 150);
    CHECK(wall.x < 50);
    // Without a cell's own walls, its colour from above darkened.
    const auto plain = texel(z.picture, 62, z.top_height + render::kProxyStripGap + 81); // (1000, 1300)
    CHECK(plain.x > 100);
    CHECK(plain.x < 200);
    // Cancelled: nothing.
    std::atomic<bool> cancel{true};
    CHECK_FALSE(render::bake_zone_proxy(test_zone(), 1, &cancel).valid());
}

TEST_CASE("zone proxy: the cache round-trips and refuses a damaged file") {
    const auto z = render::bake_zone_proxy(test_zone(), 1234);
    REQUIRE(z.valid());
    const auto bytes = render::write_zone_proxy(z);
    const auto back = render::read_zone_proxy(bytes);
    REQUIRE(back);
    CHECK(back->zone == "testzone");
    CHECK(back->key == 1234);
    CHECK(back->nx == z.nx);
    CHECK(back->ny == z.ny);
    CHECK(back->light == z.light);
    CHECK(back->drawn == z.drawn);
    for (std::size_t i = 0; i < z.height.size(); i += 97)
        CHECK((std::isnan(z.height[i]) ? std::isnan(back->height[i]) : back->height[i] == z.height[i]));
    CHECK(back->picture.mips[0] == z.picture.mips[0]);
    REQUIRE(back->trees.size() == 1);
    CHECK(back->trees[0].transform == z.trees[0].transform);
    CHECK(back->tree_kinds[0].image.mips[0] == z.tree_kinds[0].image.mips[0]);
    const auto header = render::read_zone_proxy_header(bytes);
    REQUIRE(header);
    CHECK(header->first == "testzone");
    CHECK(header->second == 1234);
    // Cut short anywhere, or a byte of its magic changed: refused.
    for (std::size_t n : {std::size_t{0}, std::size_t{7}, bytes.size() / 2, bytes.size() - 1})
        CHECK_FALSE(render::read_zone_proxy(std::span(bytes).first(n)));
    auto damaged = bytes;
    damaged[0] = std::byte{0};
    CHECK_FALSE(render::read_zone_proxy(damaged));
    // One more byte than it says: refused.
    auto longer = bytes;
    longer.push_back(std::byte{0});
    CHECK_FALSE(render::read_zone_proxy(longer));
}

namespace {

// The land of a flat zone 2000 units square at 0 (render/horizon.h).
render::HorizonField flat_field() {
    std::vector<render::HorizonTriangle> tris;
    for (float x = 0.0f; x < 2000.0f; x += 100.0f)
        for (float y = 0.0f; y < 2000.0f; y += 100.0f) {
            const glm::vec3 a(x, y, 0.0f), b(x + 100.0f, y, 0.0f), c(x + 100.0f, y + 100.0f, 0.0f), d(x, y + 100.0f, 0.0f);
            tris.push_back({a, b, c, false});
            tris.push_back({a, c, d, false});
        }
    return render::horizon_field(tris, render::horizon_land(tris, 1.0f));
}

std::size_t vertices_of(const render::ProxyZone& p) {
    std::size_t n = 0;
    for (const auto& m : p.zone.terrain) n += m.vertices.size();
    return n;
}

} // namespace

TEST_CASE("zone proxy: drawn from the zone's edge out, rising out of the land, coarser far away") {
    const auto z = render::bake_zone_proxy(test_zone(), 1);
    REQUIRE(z.valid());
    const render::HorizonField field = flat_field();
    REQUIRE(field.valid());
    CHECK(render::proxy_step(1000.0f) == 1);
    CHECK(render::proxy_step(4000.0f) == 2);
    CHECK(render::proxy_step(9000.0f) == 4);
    CHECK(render::proxy_distance(&field, {1000.0f, 1000.0f}) < 0.0f);
    CHECK(render::proxy_distance(&field, {3000.0f, 1000.0f}) == doctest::Approx(1000.0f).epsilon(0.1));
    CHECK(render::proxy_distance(nullptr, {0.0f, 0.0f}) > 1e30f);

    SUBCASE("without a field, all of it") {
        const auto p = render::proxy_zone(z, nullptr, glm::vec3(5000.0f, 0.0f, 0.0f), {});
        CHECK(p.triangles > 0);
        CHECK(p.trees == 1);
        CHECK(p.footprint.empty());
        CHECK(p.zone.generated_files.contains("hs_copy_testzone.dds"));
    }
    SUBCASE("nothing in the zone's own squares, from its edge out, the band at the edge rising out of the land") {
        // Its west part over the zone's own squares (to x 2000): left out; from the zone's edge it is drawn.
        const glm::vec3 offset(1600.0f, 0.0f, 0.0f);
        const auto p = render::proxy_zone(z, &field, offset, {});
        REQUIRE(p.triangles > 0);
        float lowest_rise = 0.0f, nearest = std::numeric_limits<float>::max();
        for (const auto& m : p.zone.terrain)
            for (const auto& v : m.vertices) {
                const glm::vec2 at = glm::vec2(v.position) + glm::vec2(offset);
                const float d = render::proxy_distance(&field, at);
                CHECK(d >= render::kProxyNear - 2.0f * render::kProxyCell);
                nearest = std::min(nearest, d);
                // In the band, under its own height (the ground there is 0).
                if (d < render::kProxyNear + 0.5f * render::kProxyFade && v.position.x > 300.0f)
                    lowest_rise = std::min(lowest_rise, v.position.z);
            }
        // It reaches the zone's edge (the field's first square past it, its middle half a square out): no gap where the
        // neighbour zone was drawn live.
        CHECK(nearest <= render::kProxyNear + 0.5f * render::kHorizonCell);
        CHECK(lowest_rise < -10.0f);
        // The land's squares it covers whole past the band, each one's middle past the band.
        REQUIRE_FALSE(p.footprint.empty());
        CHECK(std::is_sorted(p.footprint.begin(), p.footprint.end()));
        const auto cols = static_cast<std::int64_t>(field.xs.size());
        for (std::int64_t sq : p.footprint) {
            const auto c = static_cast<std::size_t>(sq % cols), r = static_cast<std::size_t>(sq / cols);
            const glm::vec2 mid(0.5f * (field.xs[c] + field.xs[c + 1]), 0.5f * (field.ys[r] + field.ys[r + 1]));
            CHECK(render::proxy_distance(&field, mid) >= render::kProxyNear + render::kProxyFade);
        }
        // Its tree stands past the band: kept.
        CHECK(p.trees == 1);
    }
    SUBCASE("far away, fewer vertices") {
        const auto near = render::proxy_zone(z, &field, glm::vec3(3500.0f, 0.0f, 0.0f), {});
        const auto far = render::proxy_zone(z, &field, glm::vec3(9000.0f, 0.0f, 0.0f), {});
        REQUIRE(vertices_of(near) > 0);
        REQUIRE(vertices_of(far) > 0);
        CHECK(vertices_of(far) * 4 < vertices_of(near));
    }
    SUBCASE("a cut plane keeps one side") {
        // Kept where x >= 6000 (in the current zone's coordinates): the copy's cells east of its own x 1000.
        const glm::vec4 keep(1.0f, 0.0f, 0.0f, -6000.0f);
        const auto p = render::proxy_zone(z, &field, glm::vec3(5000.0f, 0.0f, 0.0f), std::span(&keep, 1));
        REQUIRE(p.triangles > 0);
        for (const auto& m : p.zone.terrain)
            for (const auto& v : m.vertices) CHECK(v.position.x >= 1000.0f - 2.0f * render::kProxyCell);
    }
}
