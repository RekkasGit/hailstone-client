#pragma once

// The zone's own ladders: placed static objects (objects.wld ActorInstances)
// whose actor tag starts with LADDER, which EQObject_Construct @0x503e20
// makes actors of type 6 (Ladder). Qeynos, Guk, Unrest, Blackburrow, Ak'Anon
// and some thirty other zones have them (LADDER14 .. LADDER60, LADDERCB101 ...).
//
// Two things follow from the actor type in the original:
//  - the movement sweep's hit callback (CollisionHit_SetCollidingType) sets
//    the player's CollidingType to 6 when the sweep keeps a hit on one, so
//    walking into a ladder climbs it (no gravity; the pitch lifts the run
//    push, PhysicsObject::GetEnvCoefficients / run_speed_push);
//  - a click on one runs Ladder_MountClickedLadder (game/mechanics/ladder.h).
// Their triangles stay in the static zone mesh (game/collision.h) marked
// kTriangleLadder, so floors, sight and picking see them as before.
// Cabilis's LADDERCB101 has every polygon passable: it never stops the sweep and
// in the original is only mounted by a click (Hailstone's [movement]
// passable_ladders also climbs it, CollisionMesh::touches_passable_ladder).
//
// Hailstone's solid ladders ([movement] solid_ladders, on by default): the
// player's movement meets a ladder as one solid slab, the box of its solid
// polygons, instead of its rails and rungs. With the stand-in sweep radius
// (docs/unknowns.md) a small race's sphere passes between the rungs and drops
// off the ladder, and CanIFitHere's floor and ceiling probes find the rungs
// below and above (3.7 apart), refuse the move and leave every race stuck
// partway up. Sight, picking and the navigation mesh see the rungs as before.

#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "game/mechanics/ladder.h"
#include "game/object_collision.h"

namespace hailstone::assets { struct Zone; }

namespace hailstone::game {

// EQObject_Construct's test: the tag's first six characters are LADDER, in any case.
bool is_ladder_actor(std::string_view tag);

struct ZoneLadder {
    glm::mat4 transform{1.0f};
    ObjectShape shape;           // model space; the records include the passable triangles, marked so
    glm::vec3 center{0.0f};      // the placement's origin and the shape's radius at its scale (the pick's
    float bound = 0.0f;          // bounding sphere, as WorldObjects::place)
    mechanics::LadderSwitch ladder; // as Ladder_MountClickedLadder reads the actor
};

// Every ladder placement of a zone, in placement order. The heading is the
// placement's first angle (recovered from the transform's x axis, which the
// later Y and X turns leave pointing the same way on the ground). The height
// (the actor's vtable +0x60) is not traced: the top of the solid bounds at the
// placement's scale is used, as for a ground object (app/world_objects_ladder.cpp).
std::vector<ZoneLadder> zone_ladders(const assets::Zone& zone);

// The solid slab of a ladder placement: the 12 triangles of the box around the mesh's polygons that are not passable,
// in the placement's frame (so it leans as the ladder does), each facing out of the box, flagged kTriangleObject |
// kTriangleLadder | kTriangleLadderSlab. Empty when the ladder has no such polygon (Cabilis's LADDERCB101).
std::vector<CollisionTriangle> ladder_slab(const assets::Mesh& mesh, const glm::mat4& transform);

} // namespace hailstone::game
