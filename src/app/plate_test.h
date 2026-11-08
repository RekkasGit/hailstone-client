#pragma once

// The viewer's --plate-test (docs/graphics.md "Name plates"): test name plates over the zone, each placed from the
// CPU's rays into it (game::CollisionMesh::first_hit) so that it is known what the scene's depth should do to it.
// tools/probe_test.py runs the viewer and checks the pictures.

#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "game/collision.h"

namespace nmsc::app {

// The view the rays are cast from (render::FlyCamera's, and the scene target's size).
struct ViewRays {
    glm::vec3 position{0.0f}, forward{1.0f, 0.0f, 0.0f}, right{0.0f, -1.0f, 0.0f}, up{0.0f, 0.0f, 1.0f};
    float tan_half_x = 1.0f, tan_half_y = 1.0f;
    int width = 1, height = 1;
    float far_plane = 1000.0f;
};

// The view depth of the first surface the ray through scene pixel (px, py) meets, if any.
std::optional<float> surface_depth(const game::CollisionMesh& collision, const ViewRays& view, float px, float py);

// Front: the whole box lies in front of every surface it covers (all of it must show). Behind: the whole box lies
// behind them (none of it may). Partial: the surface is nearer than the box in some places and farther in others (it
// must be cut; not asserted).
enum class PlateKind { Front, Behind, Partial };
struct TestPlate {
    int id = 0;
    PlateKind kind = PlateKind::Front;
    glm::vec2 top_middle{0.0f}; // scene pixels: the middle of the box's top edge
    float depth = 0.0f;
};

inline constexpr float kPlateSpacing = 10.0f; // view units in front of, or behind, the surfaces under the box
inline constexpr float kPlateMinHit = 20.0f;  // a nearer surface under the box leaves the spot without a plate

// Boxes `box_w` x `box_h` scene pixels on a `columns` x `rows` grid, the kinds in turn; a spot whose box covers sky or
// a surface nearer than kPlateMinHit gets none, nor does a Partial one with no surface to be cut on.
std::vector<TestPlate> make_test_plates(const game::CollisionMesh& collision, const ViewRays& view, int columns, int rows,
                                        float box_w, float box_h);

} // namespace nmsc::app
