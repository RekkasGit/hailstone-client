#pragma once

// Find trail geometry for the server's PlayerPath and the Find window's guide path: a ground ribbon or thin,
// winding rainbow strands hovering above the terrain (a Hailstone look; docs/navigation.md "Find").

#include <functional>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

namespace hailstone::game {

enum class FindTrailStyle { Navigation = 0, Rainbow = 1 };
constexpr FindTrailStyle normalize_find_trail_style(int value) {
    return value == static_cast<int>(FindTrailStyle::Rainbow) ? FindTrailStyle::Rainbow : FindTrailStyle::Navigation;
}

struct TrailStyle {
    glm::vec3 color{1.0f};     // the single strand's colour when rainbow is off
    bool rainbow = true;
    float strand_spacing = 0.42f;
    float sway = 0.10f;        // gentle sideways movement; height always comes from the ground
    float spacing = 0.4f;      // between samples along the path
    float size = 0.40f;        // a sample's quad
    float lift = 0.35f;        // above the floor
    float alpha = 0.5f;        // between the bands
    float near_start = 1.5f, near_end = 5.0f;   // faded in from the player over this stretch
    float far_start = 120.0f, far_end = 170.0f; // faded out over this stretch
    float band_speed = 9.0f;   // units a second, towards the destination
    float band_length = 10.0f; // from one band to the next
    float band_boost = 1.4f;   // a band's extra alpha (times `alpha`) and size (x 0.5)
    bool beacon = true;        // the column of light at the end
};

struct TrailSprite {
    glm::vec3 position{0.0f};
    float size = 1.0f;
    float alpha = 0.0f;
    glm::vec3 color{1.0f};
    float distance = 0.0f; // along the route; lets connected geometry preserve gaps between supported samples
};

// A narrow strip or strand segment. Across and forward are orthogonal unit vectors; width and length are its
// full extents. Navigation uses the local ground plane, with paired dark border and gold centre (another 0.02 up).
// Rainbow strands use forward as their 3D segment axis; the renderer faces their width towards the camera.
struct TrailRibbonPiece {
    glm::vec3 position{0.0f};
    glm::vec3 across{0.0f, 1.0f, 0.0f};
    glm::vec3 forward{1.0f, 0.0f, 0.0f};
    float width = 0.0f, length = 0.0f;
    glm::vec3 color{1.0f};
    float alpha = 0.0f;
};

// The highest visible, upward-facing solid ground no higher than the point, or none.
using TrailFloorFn = std::function<std::optional<float>(const glm::vec3&)>;

// The sprites of a line that starts at the player and ends at the destination, at `time` seconds (the bands move).
// Ground is followed from the start in steps no longer than one unit, allowing local rises/drops of up to three.
// Missing or discontinuous ground hides that segment until a route vertex supports a fresh start. Anchors first
// look 0.5 above the route and up to six below, then retry upwards in 0.5 steps to three above only when needed.
// Each lateral strand needs its own support. Without a ground callback nothing is drawn.
// At most `max_sprites` in total, including the beacon (the nearest trail samples take priority).
std::vector<TrailSprite> build_trail(std::span<const glm::vec3> line, const TrailStyle& style, double time,
                                     const TrailFloorFn& floor = {}, std::size_t max_sprites = 3200);

// An MQ2Nav-style ground ribbon, using the same continuous ground sampling as the rainbow trail. Unsupported
// samples break the strip; its width also needs ground. No beacon: this style is a clear route line.
std::vector<TrailRibbonPiece> build_navigation_trail(std::span<const glm::vec3> line, const TrailStyle& style, double time,
                                                   const TrailFloorFn& floor = {}, std::size_t max_pieces = 1200);

// Fourteen continuous threads (two of each rainbow colour), 0.06 wide, hovering 3 +/- 0.7 above their local ground
// and weaving sideways within +/- 0.8. Terrain is sampled once along the centre; each strand point is grounded and
// cached, with a support check between points. Ground gaps and ledges break a thread, rather than bridging them.
std::vector<TrailRibbonPiece> build_rainbow_strands(std::span<const glm::vec3> line, const TrailStyle& style, double time,
                                                 const TrailFloorFn& floor = {}, std::size_t max_pieces = 7000);

} // namespace hailstone::game
