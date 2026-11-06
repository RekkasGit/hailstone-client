#pragma once

// MacroQuest's spawn search text (Spawn[...], SpawnCount[...], NearestSpawn[...]; MQ's ParseSearchSpawn): words like
// "npc radius 50 zradius 20 range 10 20 named", "pc =Soandso", "id 123", "corpse", "class wizard", "loc 100 -200".
// A word that is not a keyword is a (partial, case-insensitive) name; "=name" or "exact" asks for the whole name.
// Unknown keywords that take no argument are ignored, as MQ does.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec3.hpp>

namespace nmsc::game { struct Spawn; struct WorldState; struct GroupState; }

namespace nmsc::mq {

struct SpawnSearch {
    enum class Kind : std::uint8_t { Any, Pc, Npc, Corpse, NpcCorpse, PcCorpse, Pet, Mercenary };
    Kind kind = Kind::Any;
    std::optional<std::uint32_t> id;
    std::vector<std::uint32_t> not_ids;
    int min_level = 0, max_level = 1000;
    float radius = 0, zradius = 0; // 0 = no limit
    std::string name;
    bool exact = false;
    std::string class_name, race_name;
    bool named = false, targetable = false, group = false, nopet = false, gm = false, trader = false, buyer = false,
         afk = false, lfg = false, linkdead = false;
    std::optional<glm::vec3> loc; // world coordinates (game/coords.h) the distances are from; none = the player
    bool none = false;            // an id or loc no spawn can match (out of range, NaN): nothing is found
};

SpawnSearch parse_spawn_search(std::string_view text);

// MQ's IsNamed: an NPC whose name is not "a ..." / "an ..." and starts with a capital or '#'.
bool is_named(const game::Spawn& s);
// The spawn's current place (its display position once the client moved it, else the last update's).
glm::vec3 spawn_position(const game::Spawn& s);

// The spawns the search matches, nearest first. `group` (may be null) answers the "group" keyword.
std::vector<const game::Spawn*> search_spawns(const SpawnSearch& search, const game::WorldState& world,
                                              const game::GroupState* group);

} // namespace nmsc::mq
