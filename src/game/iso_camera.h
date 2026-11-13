#pragma once

// Hailstone's isometric camera (game/cameras.h kCameraIsometric, 7). The original has no such camera: this is a
// Hailstone addition (docs/isometric.md), off unless the player chooses it, and it changes only where the scene is
// seen from. Movement, facing and every game rule stay the original's.
//
// The camera looks down at a fixed pitch (45 degrees by default) from a distance, through a perspective lens (the
// stock cameras' field of view by default; /camera fov narrows it toward a flatter, more isometric look), so every
// pass that needs a perspective camera (the shadow cascades, the AO's position rebuild, the pick ray) works
// unchanged. It turns freely about the vertical and zooms; it follows the
// player with a little easing and does not stop at scenery (what stands between it and the player is the
// occlusion's to handle, not a sweep's).
//
// Being far from the player, it needs the view measured from the player rather than from itself: the fog and the
// far plane are pushed out by the camera's distance (iso_view), and the shadow cascades start near the player's depth
// (iso_shadow_near), not in the empty air in front of the lens.

#include <glm/glm.hpp>

#include "game/view_distance.h"

namespace hailstone::game {

struct IsoCameraSettings {
    float pitch = 0.7853982f;    // radians below the horizon (45 degrees)
    float fov_degrees = 63.0f;   // the horizontal field of view (about the stock cameras' 63.3)
    float distance = 160.0f;     // from the focus, at the start
    float min_distance = 60.0f;
    float max_distance = 320.0f;
    float zoom_step = 0.1f;      // a wheel notch changes the distance by this share
    float follow_tau = 0.08f;    // seconds: the easing toward the player
    float snap_distance = 50.0f; // a move further than this in one frame (a teleport, a zone-in) is not eased
};

struct IsoCamera {
    float yaw = 0.0f;           // where the camera looks (radians, game/coords.h: counterclockwise from +X)
    float distance = 160.0f;    // from the focus
    glm::vec3 focus{0.0f};      // the eased point the camera looks at
    bool placed = false;        // focus was set once
};

struct IsoPose {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;         // radians, up positive (so negative: looking down)
    float focus_distance = 0.0f;
};

// The look direction for a yaw and a downward pitch (radians below the horizon).
glm::vec3 iso_forward(float yaw, float pitch_down);

// One frame: eases the focus toward `target` over `dt` seconds and places the camera.
IsoPose update_iso_camera(IsoCamera& cam, const glm::vec3& target, float dt, const IsoCameraSettings& s);

// The wheel: positive notches zoom in (nearer), each by s.zoom_step of the distance, within the settings' range.
void iso_zoom(IsoCamera& cam, float notches, const IsoCameraSettings& s);

// Turns the camera about the vertical by `d_yaw` radians, kept within -pi..pi.
void iso_turn(IsoCamera& cam, float d_yaw);

// A field of view in degrees as the camera's fov units (512ths of a circle, game/view_distance.h camera_fov).
float iso_fov_units(float degrees);

// The view as the renderers take it with the camera `focus_distance` from the player: the fog's start and end and the
// far plane pushed out by that distance, so the player stands where the fog would start in first person and as much
// of the zone shows around them.
ViewDistance iso_view(const ViewDistance& v, float focus_distance);

// How far in front of the lens the shadow cascades start: a little short of the player, so the nearest (sharpest)
// cascade covers the player and not the air between the camera and them. Never less than `near_plane`.
inline constexpr float kIsoShadowLead = 40.0f;
float iso_shadow_near(float focus_distance, float near_plane);

// Movement relative to the camera (hailstone.ini camera.iso_wasd = camera, the default): `forward` up the screen and
// `right` across it (-1..1 each: the movement keys, or the gamepad's stick) as the world heading the player turns to
// and runs along. Nothing to move when they cancel out (or are within `dead` of the middle).
struct IsoMove {
    bool move = false;
    float yaw = 0.0f; // world yaw (radians, game/coords.h)
};
IsoMove iso_move(float forward, float right, float camera_yaw, float dead = 0.1f);

// With movement relative to the camera, the turn keys turn the camera at this rate (radians a second) instead of
// the player.
inline constexpr float kIsoTurnRate = 2.0f;

} // namespace hailstone::game
