#pragma once

// Boats: Hailstone's own design, not a port of the original's vehicle code (docs/mechanics.md "Boats"). EQEmu moves
// a boat NPC (a vehicle race, game/race_models.h) along its grid and sends a position update when it starts, turns or
// changes speed, and every 5 s; a rider sends its position in the boat's frame (OP_ClientUpdate's vehicle id, which
// the server adds the boat's position and heading back to).
//
// - A boat is stepped between updates as other spawns are (along its heading at its SpeedRun), and an update's
//   correction is faded in over a couple of seconds (the display error) instead of eased toward, so the drawn deck
//   neither stutters at an update nor trails behind where the server has the boat.
// - Everything on a boat lives in its frame: the local player is carried by the drawn pose's change each frame
//   (PlayerMovement), the hull and deck collide as the model's triangles re-placed at the drawn pose (ClientApp's
//   vehicles), and other spawns standing on it keep their place on the deck between their updates (riders below).
// - While a boat moves, a player walking on it cannot walk off over its side (kRailDrop); the helm's boat stops short
//   of docks and other boats (helm_resolve, hull_probes); overlapping decks go to the nearest, the one stood on in a
//   tie (sweep_hulls).

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "game/swept_sphere.h"

namespace hailstone::game {

struct Spawn;

// A vehicle's place and heading (512ths): its drawn pose (display_*) or its stepped one (the server's, predicted).
struct BoatPose {
    glm::vec3 position{0.0f};
    float heading = 0.0f;
};
BoatPose drawn_pose(const Spawn& s);
BoatPose stepped_pose(const Spawn& s);

// World <-> the boat's frame (x along its heading), turned with the same sine table the model is drawn with
// (ActorRenderer::instance_matrix), so a point on the deck stays on the drawn deck.
glm::vec3 boat_to_local(const BoatPose& boat, const glm::vec3& world);
glm::vec3 boat_to_world(const BoatPose& boat, const glm::vec3& local);
float wrap_heading(float heading); // into 0..512

// How fast a boat's display error fades (per second), and the jump that is a teleport (no fading).
inline constexpr float kBoatSettleRate = 1.2f;
inline constexpr float kBoatSnapDistance = 60.0f;

// A vehicle's position update arrived (before it snaps the spawn to `position` / `heading`): the drawn pose stays
// where it is and the difference becomes the display error.
void vehicle_note_update(Spawn& s, const glm::vec3& position, float heading);
// One frame of a vehicle: stepped along its heading while its last update is fresh, the display error faded.
void step_vehicle(Spawn& s, float dt, bool fresh);
// The same with the physics steps given (the game clock's, core/step_clock.h: none, one or several a frame, in ms);
// the display error fades over the frame's dt.
void step_vehicle(Spawn& s, float dt, bool fresh, std::span<const float> steps_ms);

// Another spawn whose update put it on `vehicle`'s deck: its place in the vehicle's frame (from the stepped pose,
// which is what the server measures it against); the drawn place carries on from where it is drawn.
void attach_rider(Spawn& rider, std::uint32_t vehicle_id, const Spawn& vehicle);
// One frame of a rider: stepped in the vehicle's frame (the deltas a rider sends, and EQEmu passes on, are in it),
// then placed on the stepped and drawn poses; the drawn place eases toward the stepped one on the deck.
void step_rider(Spawn& rider, const Spawn& vehicle, float dt, bool fresh);
void step_rider(Spawn& rider, const Spawn& vehicle, float dt, bool fresh, std::span<const float> steps_ms);

// Rails on a moving deck (app/player_movement.cpp): a walking player whose step would leave the deck of the vehicle
// carrying it, over a floor more than this below, is kept on the deck while the vehicle moves. Flat decks such as the
// LAUNCH skiff's have no rails in their models, and a moving deck slides out from under a player who stands near its
// edge. A jump still goes over, and a still boat lets the player step off anywhere.
inline constexpr float kRailDrop = 4.0f;

// At the helm of a small boat (OP_ControlBoat, which EQEmu grants for its controllable races 141 BOAT and 502 ROW):
// the forward and back keys speed it up or slow it to a reverse crawl, the turn keys turn it, and with neither it
// drifts to a stop. The player stands still on the deck meanwhile (carried, game/boats.h).
struct Helm {
    float speed = 0.0f; // units a second along the heading (negative: astern)
    float turn = 0.0f;  // heading units a second (positive: to the left)
};
inline constexpr float kHelmTopSpeed = 14.0f;
inline constexpr float kHelmAsternSpeed = 5.0f;
inline constexpr float kHelmAcceleration = 7.0f; // units a second, each second
inline constexpr float kHelmDrag = 3.0f;         // slowing with no key held
inline constexpr float kHelmTurnRate = 40.0f;
// One frame of the helm: the new pose (the caller keeps the boat in the water and off what it may not pass,
// helm_resolve).
BoatPose steer_boat(Helm& helm, const BoatPose& pose, bool forward, bool back, bool left, bool right, float dt);

// Whether the boat may go from one pose to the next (in the water, clear of docks and other boats).
using HelmClearFn = std::function<bool(const BoatPose& from, const BoatPose& to)>;
// The helm's step against the world: the whole step when `clear` lets it; else the move without the turn (a boat
// backing off a dock with the wheel over); else the turn alone, in place (a bow swinging away); else nothing. A step
// whose move is refused stops the boat (aground, or against a dock or another boat). There is no push back or bounce: the boat stays short of what it would hit.
BoatPose helm_resolve(Helm& helm, const BoatPose& from, const BoatPose& to, const HelmClearFn& clear);

// The hull's probes for the helm's collision, in the boat's frame: small spheres (radius `probe_radius`) just
// inside its outline, one at the outermost point of the model's triangles in each of kHullProbeDirections
// directions around the boat (at that point's own height, which is where the hull is widest that way), pulled in by
// the radius toward the middle of the outline. Swept from where each probe is to where the step would take it, they find the dock or the other boat
// the hull would run into. Duplicates (a box's corner answers two directions) are dropped.
inline constexpr int kHullProbeDirections = 16;
inline constexpr float kHullProbeRadius = 1.0f;
std::vector<glm::vec3> hull_probes(std::span<const glm::vec3> points, float probe_radius = kHullProbeRadius);
// A probe's sweep: true when something stops it short of `to`. A probe already touching something where it starts
// (the hull a little into the sea floor, a dock it was spawned against) is the sweep's business: it should answer
// false for a hit at the very start, so the boat is never held for good by what it already touches.
using HullSweepFn = std::function<bool(const glm::vec3& from, const glm::vec3& to, float radius)>;
// The probes swept from the `from` pose to the `to` pose: true when none is stopped.
bool hull_clear(const BoatPose& from, const BoatPose& to, std::span<const glm::vec3> probes, const HullSweepFn& blocked,
                float probe_radius = kHullProbeRadius);

// A vehicle's hull where it is drawn this frame (ClientApp's vehicles): its model's triangles, the floors of the
// floor test and the walls of the movement sweeps, and the bounding sphere that skips the far ones.
struct VehicleHull {
    std::uint32_t id = 0;
    std::vector<CollisionTriangle> triangles;
    glm::vec3 center{0.0f};
    float radius = 0.0f;
};
// The sweep against the hulls near it, all but `skip`'s (the helm's own boat). Returns the vehicle whose triangle
// stopped it nearest (0: none did); where hulls overlap and two stop it at the same distance (two decks at one
// height), `prefer` (the vehicle the player already stands on) wins, so the floor test does not swap between them
// from frame to frame. Every hull's hit still shortens the sweep (each is a wall).
inline constexpr float kHullTieDistance = 0.01f; // units along the sweep that count as the same distance
std::uint32_t sweep_hulls(SphereSweep& s, std::span<const VehicleHull> hulls, float radius, std::uint32_t prefer = 0,
                          std::uint32_t skip = 0);

} // namespace hailstone::game
