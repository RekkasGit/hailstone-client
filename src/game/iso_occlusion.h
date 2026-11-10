#pragma once

// Seeing the player through what stands in front, in the isometric camera (game/iso_camera.h; docs/isometric.md):
// a Hailstone addition. The camera looks down from far off, so walls, slopes, trees, roofs and the floors above stand
// between it and the player. What is nearer the camera than the player, inside a cone round the line from the camera
// to the player (a circle on the screen), and higher than the player's feet is cut away (shaders/lighting.glsl
// reveal_discard: dithered, so the edge is soft without blending). The ground the player stands on and the slope
// falling away in front are below the feet and stay; what rises in front of the player goes. The shadow passes do
// not cut, so what is cut still casts its shadow.
//
// Two shapes, each with its own strength the client eases in and out:
//  - the cone, while something stands between the camera and the player (the line from the camera to the player's
//    feet, middle or head is blocked);
//  - the roof, while the player is under cover (something solid over their head): everything above the player's head
//    within a wide circle round them goes, so a room, a corridor or a cave shows from above as a cut-away, its walls
//    trimmed at head height and the floors above taken away.
//
// The same test runs here for the clicks: a click through the cut reaches what lies behind it
// (ClientApp::pick_at, ClientApp::world_point_at).

#include <glm/glm.hpp>

#include "game/picking.h"

namespace hailstone::game {

// render/scene_lighting.h SceneLighting's reveal fields, as the shaders read them.
struct IsoReveal {
    glm::vec4 origin{0.0f}; // xyz the camera, w the cone's strength (0 off, 1 full)
    glm::vec4 axis{0.0f};   // xyz the unit way from the camera to the player, w their distance
    glm::vec4 shape{0.0f};  // x the radius at the player (world units), y the height the cut starts at, z the depth
                            // margin before the player, w the soft edge's share of the radius
    glm::vec4 roof{0.0f};   // x the height above which it cuts, y its radius round the player (flat), z the width of
                            // its soft edge, w its strength (0 off)
};

struct IsoRevealInput {
    glm::vec3 camera{0.0f};
    glm::vec3 focus{0.0f};       // the middle of the player's body
    float feet_z = 0.0f;
    float radius = 12.0f;        // at the player
    float strength = 1.0f;       // the cone's
    float roof_height = 0.0f;    // the roof's cut: above this height
    float roof_radius = 60.0f;
    float roof_strength = 0.0f;
};

// The roof's soft edge (flat units), and how far over its height it rises to a full cut.
inline constexpr float kIsoRoofSoft = 15.0f;
inline constexpr float kIsoRoofRise = 1.5f;
// The roof cuts above the player's eyes by this much.
inline constexpr float kIsoRoofAboveEyes = 1.5f;
// Cover: something solid within this height over the player's head.
inline constexpr float kIsoCoverReach = 60.0f;
// The strengths ease in over this long (seconds) and out over kIsoEaseOut.
inline constexpr float kIsoEaseIn = 0.2f;
inline constexpr float kIsoEaseOut = 0.5f;

// One frame of a strength easing toward `target` (0 or 1): in over kIsoEaseIn, out over kIsoEaseOut, linearly.
float iso_ease(float current, float target, float dt);

// Above the feet by this much before anything is cut (a step or a kerb at the player's feet stays), and fully only
// kIsoRevealRise higher.
inline constexpr float kIsoRevealFloor = 1.0f;
inline constexpr float kIsoRevealRise = 3.0f;
// Only what is at least this much nearer the camera than the player's middle (the body's own depth), and fully only
// kIsoRevealDepthFade nearer still.
inline constexpr float kIsoRevealMargin = 3.0f;
inline constexpr float kIsoRevealDepthFade = 6.0f;
// The outer share of the radius the cut fades over.
inline constexpr float kIsoRevealSoft = 0.6f;
// The radius setting is at the isometric camera's starting distance (game::IsoCameraSettings::distance); zoomed in or
// out it scales with the distance, so the circle keeps its size on the screen.
inline constexpr float kIsoRevealAtDistance = 160.0f;

// Off (all zero) when the camera and the player coincide or both strengths are 0.
IsoReveal iso_reveal(const IsoRevealInput& in);

// How much of world point `p` the cut takes, 0 none .. 1 all (the shader drops a pixel when this is above its dither
// threshold). The shader's reveal_cut, line for line (its constants are these).
float iso_reveal_cut(const IsoReveal& r, const glm::vec3& p);

// A pick's world triangle the cut takes away where the segment meets it (at least half cut there, as the dither
// drops at least half of such pixels): a click goes on through it. False where the segment misses the triangle.
bool iso_pick_skips(const IsoReveal& r, const PickSegment& s, const PickTriangle& t);

} // namespace hailstone::game
