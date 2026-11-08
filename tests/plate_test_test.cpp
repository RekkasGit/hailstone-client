// The CPU side of the viewer's --plate-test (app/plate_test.h): where the test plates go.

#include <doctest/doctest.h>

#include "app/plate_test.h"

using namespace nmsc;
using namespace nmsc::app;

namespace {

// A wall across the view, 100 ahead of a camera at the origin looking along +X. It is made with both windings, so
// one of them faces the camera whichever way CollisionTriangle::make turns its normal.
game::CollisionMesh wall() {
    using game::CollisionTriangle;
    const glm::vec3 a(100, -500, -500), b(100, 500, -500), c(100, 500, 500), d(100, -500, 500);
    return game::CollisionMesh::from_triangles({CollisionTriangle::make(a, c, b, 0), CollisionTriangle::make(a, d, c, 0),
                                                CollisionTriangle::make(a, b, c, 0), CollisionTriangle::make(a, c, d, 0)});
}

ViewRays view() {
    ViewRays v;
    v.tan_half_x = v.tan_half_y = 1.0f;
    v.width = 640;
    v.height = 360;
    v.far_plane = 1000.0f;
    return v;
}

} // namespace

TEST_CASE("test plates: the surface depth is the distance along the view, whichever way the ray leans") {
    const auto mesh = wall();
    for (float px : {10.0f, 320.0f, 630.0f})
        for (float py : {10.0f, 180.0f, 350.0f}) {
            const auto d = surface_depth(mesh, view(), px, py);
            REQUIRE(d.has_value());
            CHECK(*d == doctest::Approx(100.0f).epsilon(0.001));
        }
}

TEST_CASE("test plates: on a flat wall, plates in front of and behind it, none partial") {
    const auto mesh = wall();
    const auto plates = make_test_plates(mesh, view(), 6, 3, 60.0f, 30.0f);
    REQUIRE(!plates.empty());
    int front = 0, behind = 0, partial = 0;
    for (const auto& p : plates) {
        if (p.kind == PlateKind::Front) {
            ++front;
            CHECK(p.depth == doctest::Approx(100.0f - kPlateSpacing).epsilon(0.001));
        } else if (p.kind == PlateKind::Behind) {
            ++behind;
            CHECK(p.depth == doctest::Approx(100.0f + kPlateSpacing).epsilon(0.001));
        } else {
            ++partial;
        }
    }
    CHECK(front > 0);
    CHECK(behind > 0);
    CHECK(partial == 0); // a flat wall has no edge to cut a plate on
}

TEST_CASE("test plates: a view with nothing to hit has none") {
    const auto mesh = game::CollisionMesh::from_triangles({});
    CHECK(make_test_plates(mesh, view(), 6, 3, 60.0f, 30.0f).empty());
    CHECK_FALSE(surface_depth(mesh, view(), 320.0f, 180.0f).has_value());
}
