#pragma once

// Whether a live asset folder's revamped .eqg (assets::is_revamped_zone_name: arena, bazaar, highpasshold,
// lavastorm, nektulos) is the zone the server runs (docs/layout.md "Zones live replaced").
//
// A deliberate difference from the original, which loads a zone by its name alone (CDisplay::StartWorldDisplay
// @0x49b200 tries <zone>.EQG first). A stock EQEmu server can run either geometry under the one name: PEQ's zone
// table has a row per zone version, and the server picks the version from its expansion setting (Bazaar version 1
// from expansion 9). RoF2's zone-in packets do not carry the version, so the client tests the safe point
// OP_NewZone sends: the zone the server runs has a floor just under it. Measured on PEQ's rows against the live
// files: the revamp's own safe points stand 2.0 to 4.2 units above its floor, and the old zones' have no floor
// under them there or one 15.9 units down (Nektulos version 0).

#include <optional>

#include <glm/glm.hpp>

#include "assets/zone.h"
#include "game/collision.h"

namespace hailstone::game {

// The step up of the floor test (the player's own floor probe uses 4), and how high above the floor the safe point
// may stand.
inline constexpr float kSafePointStepUp = 4.0f;
inline constexpr float kSafePointMaxHeight = 10.0f;

// How high the safe point (world coordinates) stands above the highest floor no higher than it plus the step up,
// or nullopt when there is none.
std::optional<float> safe_point_height(const CollisionMesh& mesh, const glm::vec3& safe_world);

// True when the safe point stands on the geometry: a floor at most kSafePointMaxHeight under it (or up to the step
// up over it).
bool safe_point_on_floor(const CollisionMesh& mesh, const glm::vec3& safe_world);

// The test load_zone asks before it keeps a revamped .eqg: builds the zone's collision mesh into `built` (so the
// caller can keep it rather than build it again) and checks the safe point (world coordinates) against it. `built`
// must outlive the returned test.
assets::RevampedZoneTest safe_point_test(const glm::vec3& safe_world, std::optional<CollisionMesh>& built);

} // namespace hailstone::game
