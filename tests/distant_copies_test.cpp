#include <doctest/doctest.h>

#include <vector>

#include <glm/glm.hpp>

#include "app/distant_copies.h"

using namespace hailstone;

TEST_CASE("distant copies: the atlas's zones, then the neighbours it did not place, each once") {
    app::AtlasLand atlas;
    atlas.placed_zones.push_back({"qeynos2", glm::vec3(-1750.0f, 0.0f, -8.0f), 1});
    atlas.placed_zones.push_back({"qeynos", glm::vec3(-2386.0f, 433.0f, -8.0f), 2});
    atlas.cuts["qeynos2"] = {glm::vec4(-1.0f, 0.0f, 0.0f, -350.0f)};

    std::vector<app::NeighbourZones::CopyTarget> neighbours;
    // Placed by the atlas too: its placement wins.
    neighbours.push_back({"qeynos2", glm::vec3(-1700.0f, 0.0f, 0.0f), glm::vec4(-1.0f, 0.0f, 0.0f, -300.0f)});
    // Not in the atlas (not visited yet): at its own offset, kept past its seam.
    neighbours.push_back({"qey2hh1", glm::vec3(1185.0f, -2528.0f, -19.0f), glm::vec4(0.0f, -1.0f, 0.0f, -2468.0f)});

    const auto placed = app::copy_placements(atlas, neighbours);
    REQUIRE(placed.size() == 3);
    CHECK(placed[0].zone == "qeynos2");
    CHECK(placed[0].offset == glm::vec3(-1750.0f, 0.0f, -8.0f));
    REQUIRE(placed[0].keep.size() == 1);
    CHECK(placed[0].keep[0].w == doctest::Approx(-350.0f));
    CHECK(placed[1].zone == "qeynos");
    CHECK(placed[1].keep.empty());
    CHECK(placed[2].zone == "qey2hh1");
    CHECK(placed[2].offset == glm::vec3(1185.0f, -2528.0f, -19.0f));
    REQUIRE(placed[2].keep.size() == 1);
    CHECK(placed[2].keep[0] == glm::vec4(0.0f, -1.0f, 0.0f, -2468.0f));

    SUBCASE("no atlas: every neighbour") {
        const auto only = app::copy_placements(app::AtlasLand{}, neighbours);
        REQUIRE(only.size() == 2);
        CHECK(only[0].zone == "qeynos2");
        CHECK(only[1].zone == "qey2hh1");
    }
}
