#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "assets/wld_detail.h"
#include "assets/zone.h"
#include "game_files.h"

using namespace nmsc;
using namespace nmsc::assets;

namespace {

// An uncompressed 8-bit BMP, rows given in storage order.
std::vector<std::byte> bmp8(std::uint32_t w, std::uint32_t h, const std::vector<std::uint8_t>& rows) {
    std::uint32_t stride = (w + 3) & ~3u;
    std::uint32_t offset = 14 + 40 + 256 * 4;
    std::vector<std::byte> d(offset + stride * h);
    auto put32 = [&](std::size_t at, std::uint32_t v) { std::memcpy(d.data() + at, &v, 4); };
    auto put16 = [&](std::size_t at, std::uint16_t v) { std::memcpy(d.data() + at, &v, 2); };
    d[0] = std::byte{'B'};
    d[1] = std::byte{'M'};
    put32(2, static_cast<std::uint32_t>(d.size()));
    put32(10, offset);
    put32(14, 40);
    put32(18, w);
    put32(22, h);
    put16(26, 1);
    put16(28, 8);
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) d[offset + y * stride + x] = std::byte{rows[y * w + x]};
    return d;
}

} // namespace

TEST_CASE("BMINFO names: a _DETAIL second name is one detail layer with its atof scale") {
    Material m;
    wld::apply_detail_names({"TRANQSANDCM.DDS", "TRANQSANDDET01.DDS_DETAIL_80.000000"}, m);
    REQUIRE(m.detail_layers.size() == 1);
    CHECK(m.detail_layers[0].texture == "TRANQSANDDET01.DDS");
    CHECK(m.detail_layers[0].uv_scale == doctest::Approx(80.0f));
    CHECK(m.detail_palette.empty());

    Material layer;
    wld::apply_detail_names({"HUMHE0001.DDS", "HUMHE0001_LAYER.DDS"}, layer); // a character layer
    CHECK(layer.detail_layers.empty());
    Material plain;
    wld::apply_detail_names({"WALL.DDS"}, plain);
    CHECK(plain.detail_layers.empty());
}

TEST_CASE("BMINFO names: three or more are a palette map and its entries, x 18") {
    Material m;
    wld::apply_detail_names({"TRANQBASE2A.DDS", "TRANQBASE2APAL.BMP", "1, 3, 2, TRANQSANDDET01.DDS",
                             "4, 3, 5, TRANQGRASSDET02.DDS", "5, 2, 0, TRANQROCKDET01.DDS", "no commas"},
                            m);
    CHECK(m.detail_palette == "TRANQBASE2APAL.BMP");
    REQUIRE(m.detail_layers.size() == 4);
    CHECK(m.detail_layers[0].texture == "TRANQSANDDET01.DDS");
    CHECK(m.detail_layers[0].uv_scale == doctest::Approx(54.0f));
    CHECK(m.detail_layers[1].texture == "TRANQGRASSDET02.DDS");
    CHECK(m.detail_layers[2].uv_scale == doctest::Approx(36.0f));
    CHECK(m.detail_layers[3].texture.empty()); // keeps its place, draws nothing

    // A scale past int32 wraps as the original's 32-bit atoi does, the same on every platform (glibc's atoi
    // saturated it to -1 here, MSVC's to INT_MAX).
    Material wide;
    wld::apply_detail_names({"A.DDS", "APAL.BMP", "1, 99999999999999999999, 2, X.DDS"}, wide);
    REQUIRE(wide.detail_layers.size() == 1);
    CHECK(wide.detail_layers[0].uv_scale == doctest::Approx(1661992959.0f * 18.0f));

    // Seventeen names: the loader keeps only the diffuse.
    std::vector<std::string> many{"A.DDS", "APAL.BMP"};
    for (int i = 0; i < 15; ++i) many.push_back("1, 1, 1, X.DDS");
    Material none;
    wld::apply_detail_names(many, none);
    CHECK(none.detail_layers.empty());
    CHECK(none.detail_palette.empty());
}

TEST_CASE("Palette maps keep a bottom-up BMP's storage order and sample as the region builder does") {
    // 4 x 2, stored rows: {0, 1, 2, 3} then {4, 0xff, 1, 1}.
    auto data = bmp8(4, 2, {0, 1, 2, 3, 4, 0xff, 1, 1});
    auto map = wld::decode_palette_map(data);
    REQUIRE(map);
    CHECK(map->width == 4);
    CHECK(map->height == 2);
    CHECK(wld::palette_entry(*map, {0.0f, 0.0f}, 5) == 0);
    CHECK(wld::palette_entry(*map, {0.25f, 0.0f}, 5) == 1);  // x = round(1.0)
    CHECK(wld::palette_entry(*map, {0.0f, 0.5f}, 5) == 4);   // the second stored row is v = 0.5
    CHECK(wld::palette_entry(*map, {0.375f, 0.0f}, 5) == 2); // round(1.5) = 2 (to even)
    CHECK(wld::palette_entry(*map, {0.625f, 0.0f}, 5) == 2); // round(2.5) = 2 (to even)
    CHECK(wld::palette_entry(*map, {-0.25f, 0.0f}, 5) == 3); // -1 as unsigned, modulo 4
    CHECK(wld::palette_entry(*map, {1.0f, 1.0f}, 5) == 0);   // wraps
    CHECK(wld::palette_entry(*map, {0.75f, 0.0f}, 3) == -1); // 3 is not below 3 entries
    CHECK(wld::palette_entry(*map, {0.25f, 0.5f}, 5) == -1); // 0xff is -1 as a signed char
    CHECK_FALSE(wld::decode_palette_map(std::vector<std::byte>(10)));
}

TEST_CASE("Palette maps: a top-down BMP keeps v = 0 on the picture's bottom row; INT32_MIN height is refused") {
    // The same picture as bottom-up {0, 1, 2, 3} then {4, 5, 6, 7}, stored top-down: {4, 5, 6, 7} first.
    auto data = bmp8(4, 2, {4, 5, 6, 7, 0, 1, 2, 3});
    const std::int32_t minus_two = -2;
    std::memcpy(data.data() + 22, &minus_two, 4);
    auto map = wld::decode_palette_map(data);
    REQUIRE(map);
    CHECK(map->height == 2);
    CHECK(wld::palette_entry(*map, {0.0f, 0.0f}, 8) == 0);
    CHECK(wld::palette_entry(*map, {0.25f, 0.0f}, 8) == 1);
    CHECK(wld::palette_entry(*map, {0.0f, 0.5f}, 8) == 4);
    CHECK(wld::palette_entry(*map, {0.75f, 0.5f}, 8) == 7);
    CHECK(map->pixels == wld::decode_palette_map(bmp8(4, 2, {0, 1, 2, 3, 4, 5, 6, 7}))->pixels);

    const std::int32_t lowest = INT32_MIN;
    std::memcpy(data.data() + 22, &lowest, 4);
    CHECK_FALSE(wld::decode_palette_map(data));
    const std::int32_t too_tall = -4097;
    std::memcpy(data.data() + 22, &too_tall, 4);
    CHECK_FALSE(wld::decode_palette_map(data));
}

TEST_CASE("Detail batches: per entry, reversed, grouped while under 150 units across") {
    Mesh mesh;
    // A 2 x 1 palette map: u < 0.25 is entry 0, else entry 1.
    auto data = bmp8(2, 1, {0, 1});
    auto map = wld::decode_palette_map(data);
    REQUIRE(map);
    auto vertex = [&](float x, float y, float u) {
        Vertex v;
        v.position = {x, y, 0.0f};
        v.uv = {u, 0.0f};
        mesh.vertices.push_back(v);
    };
    vertex(0, 0, 0.0f);   // 0: entry 0
    vertex(10, 0, 0.0f);  // 1: entry 0
    vertex(0, 10, 0.6f);  // 2: entry 1
    vertex(200, 0, 0.0f); // 3: entry 0, far away
    vertex(210, 0, 0.0f); // 4
    vertex(200, 10, 0.0f); // 5
    mesh.indices = {0, 1, 2, 3, 4, 5};
    mesh.parts.push_back({0, 6, 0, true});
    Material m;
    wld::apply_detail_names({"BASE.DDS", "PAL.BMP", "1, 1, 0, A.DDS", "2, 1, 0, B.DDS"}, m);
    mesh.materials.push_back(m);

    auto batches = wld::detail_batches(mesh, 0, &*map);
    // Reversed: triangle 1 (far, entry 0) makes the first batch; triangle 0 is too far from it for entry 0 (a new
    // batch) and also goes to a new entry-1 batch. Newest first.
    REQUIRE(batches.size() == 3);
    CHECK(batches[0].layer == 1);
    CHECK(batches[0].indices == std::vector<std::uint32_t>{0, 1, 2});
    CHECK(batches[1].layer == 0);
    CHECK(batches[1].indices == std::vector<std::uint32_t>{0, 1, 2});
    CHECK(batches[1].box == glm::vec4(0, 10, 0, 10));
    CHECK(batches[2].layer == 0);
    CHECK(batches[2].indices == std::vector<std::uint32_t>{3, 4, 5});

    Mesh single = mesh;
    single.materials[0] = Material{};
    wld::apply_detail_names({"CM.DDS", "DET.DDS_DETAIL_4.000000"}, single.materials[0]);
    auto one = wld::detail_batches(single, 0, nullptr);
    REQUIRE(one.size() == 1);
    CHECK(one[0].indices.size() == 6);
    CHECK(one[0].box == glm::vec4(0, 210, 0, 10));
}

TEST_CASE("potranquility's ground has palette detail layers" * doctest::skip(!nmsc::test::game_dir())) {
    auto z = load_classic_zone(*nmsc::test::game_dir(), "potranquility");
    std::size_t palettes = 0, singles = 0;
    for (const auto& mesh : z.terrain)
        for (const auto& m : mesh.materials) {
            if (m.detail_layers.empty()) continue;
            if (m.detail_palette.empty()) {
                ++singles;
                continue;
            }
            ++palettes;
            if (m.frames.front() == "TRANQBASE2A.DDS") {
                CHECK(m.detail_palette == "TRANQBASE2APAL.BMP");
                REQUIRE(m.detail_layers.size() == 5);
                CHECK(m.detail_layers[4].texture == "TRANQROCKDET01.DDS");
                CHECK(m.detail_layers[4].uv_scale == doctest::Approx(54.0f));
            }
        }
    CHECK(palettes > 0);
    CHECK(singles > 0);
    auto map = wld::decode_palette_map(*z.read_file("tranqbase2apal.bmp"));
    REQUIRE(map);
    CHECK(map->width == 128);
    CHECK(map->height == 128);
    // Region meshes without colours get the region loader's 0xFF1F1F1F.
    CHECK(z.terrain.front().vertices.front().color == 0xff1f1f1fu);
}
