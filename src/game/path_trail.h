#pragma once

// How a path is drawn in the world as a trail of soft glowing sprites (a Hailstone look; docs/navigation.md "Find"):
// for the find path (game/player_path.h, whose original drew a textured tube) and the Find window's guide path
// (app/guide_path.h). The line is sampled at an even spacing and each sample put on the floor, so the trail neither
// clumps at the path's corners nor gaps on its long straights, nor cuts through a hill between two corners. Bright
// bands run along it towards the destination, it fades in just ahead of the player and out far along, and a column
// of light stands at the end.

#include <functional>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

namespace hailstone::game {

struct TrailStyle {
    glm::vec3 color{1.0f};
    float spacing = 1.4f;      // between samples along the path
    float size = 1.5f;         // a sample's quad
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
};

// The floor below a point (the highest floor no higher than the point), or none.
using TrailFloorFn = std::function<std::optional<float>(const glm::vec3&)>;

// The sprites of a line that starts at the player and ends at the destination, at `time` seconds (the bands move).
// A sample whose floor is more than 6 off the line keeps the line's height (a bridge over a gorge, a floor missing).
// At most `max_sprites` along the line (the nearest kept), and the beacon's 7 at the end.
std::vector<TrailSprite> build_trail(std::span<const glm::vec3> line, const TrailStyle& style, double time,
                                     const TrailFloorFn& floor = {}, std::size_t max_sprites = 400);

} // namespace hailstone::game
