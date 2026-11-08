#pragma once

// NPC collision: Hailstone's own rule, not the original's (docs/mechanics.md "NPC collision"). In RoF2 no spawn's
// body stops the player (CollisionFilter_Movement lets players through, and an NPC's skeleton has no collision
// triangles), so everyone walks through everyone. Here a live NPC is an upright wall the player's movement sweep
// stops at and slides along; players, pets (swarm pets too), mercenaries, corpses, mounts and vehicles (game/boats.h has those) stay
// passable. A server switches it off with hailstone.cfg's [movement] npc_collision = off (plugin/policy.h); without
// the file it is on.
//
// - The body is a 12-sided upright prism around the spawn: its radius is the original's collision volume for an NPC
//   (mechanics::collision_volume_radius: AvatarHeight x 0.5 x 0.35, 0.66 for a human), from a little under its feet
//   to twice its AvatarHeight (at least kNpcMinWallHeight, so a small NPC is not stepped over). It has walls only: no top to stand on, and the
//   floor and ceiling tests never see it.
// - A sweep that starts inside a body (the NPC walked onto the player, or spawned there) ignores that body, so the
//   player is never held; the walls face outward, and a sweep outward through them never hits them anyway.

#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "game/swept_sphere.h"

namespace hailstone::game {

struct Spawn;

inline constexpr int kNpcWallSides = 12;
inline constexpr float kNpcMinWallHeight = 8.0f; // above the feet: more than the player's lifted sweep clears
inline constexpr float kNpcWallBelowFeet = 2.0f; // the walls start this far under the feet (a slope, a stair)
// A sweep starting closer to a body's axis than (its radius + the sweep's) less this counts as inside it. A sweep
// that ended against the body last frame rests at about that distance and must still be held.
inline constexpr float kNpcInsideSlack = 0.25f;

// A temporary (swarm) pet: wizard, magician and necromancer swarms, Wake the Dead, animated weapons. Stock EQEmu
// sends them with no petOwnerId, so they are told by what NPC::PetOnSpawn sets instead: a player's swarm pet is not
// cyclable (targetable_with_hotkey off) and is named "<owner>'s Pet" in its last name unless the server turned
// that off. Any NPC the server keeps off the target-cycling keys counts as one, so it does not block either.
bool is_swarm_pet(const Spawn& s);

// The spawns that stop the player: a live NPC that is no one's pet (swarm pets included), not a mercenary, not ridden and not a vehicle
// race. `controlled_id` (the player, or the mount the player rides) never blocks itself.
bool npc_blocks_player(const Spawn& s, std::uint32_t controlled_id);

// One NPC's body where it stands this frame.
struct NpcBody {
    std::uint32_t id = 0;
    glm::vec3 feet{0.0f};
    float radius = 0.0f;
    float height = 0.0f; // the walls' top above the feet
};

// The body of a spawn npc_blocks_player accepts, at `position` (its z the spawn's, AvatarHeight above its feet).
NpcBody npc_body(const Spawn& s, const glm::vec3& position);

// The body's walls as collision triangles, facing outward.
std::vector<CollisionTriangle> npc_walls(const NpcBody& body);

// The sweep against the bodies near it, all but those it starts inside. Returns the id of the body that stopped it
// nearest (0: none did); each hit shortens the sweep as a wall does.
std::uint32_t sweep_npcs(SphereSweep& s, std::span<const NpcBody> bodies);

} // namespace hailstone::game
