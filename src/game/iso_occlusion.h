#pragma once

// Seeing the player through what stands in front, in the isometric camera (game/iso_camera.h; docs/isometric.md):
// a Hailstone addition. The camera looks down from far off, so walls, slopes, trees, roofs and the floors above stand
// between it and the player. What is nearer the camera than the player, inside a cone round the line from the camera
// to the player (a circle on the screen), and higher than the player's feet is cut away (shaders/lighting.glsl
// reveal_discard: dithered, so the edge is soft without blending). The ground the player stands on and the slope
// falling away in front are below the feet and stay; what rises in front of the player goes. The shadow passes do
// not cut, so what is cut still casts its shadow.
//
// The same test runs here for the clicks: a click through the cut reaches what lies behind it
// (ClientApp::world_point_at).

#include <glm/glm.hpp>

namespace hailstone::game {

// render/scene_lighting.h SceneLighting's reveal fields, as the shaders read them.
struct IsoReveal {
    glm::vec4 origin{0.0f}; // xyz the camera, w the strength (0 off, 1 full)
    glm::vec4 axis{0.0f};   // xyz the unit way from the camera to the player, w their distance
    glm::vec4 shape{0.0f};  // x the radius at the player (world units), y the height the cut starts at, z the depth
                            // margin before the player, w the soft edge's share of the radius
};

struct IsoRevealInput {
    glm::vec3 camera{0.0f};
    glm::vec3 focus{0.0f};       // the middle of the player's body
    float feet_z = 0.0f;
    float radius = 12.0f;        // at the player
    float strength = 1.0f;
};

// Above the feet by this much before anything is cut (a step or a kerb at the player's feet stays).
inline constexpr float kIsoRevealFloor = 1.0f;
// Only what is at least this much nearer the camera than the player's middle (the body's own depth).
inline constexpr float kIsoRevealMargin = 3.0f;
// The outer share of the radius the cut fades over.
inline constexpr float kIsoRevealSoft = 0.35f;

IsoReveal iso_reveal(const IsoRevealInput& in);

// How much of world point `p` the cut takes, 0 none .. 1 all (the shader drops a pixel when this is above its dither
// threshold). The shader's reveal_cut, line for line.
float iso_reveal_cut(const IsoReveal& r, const glm::vec3& p);

} // namespace hailstone::game
