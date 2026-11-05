#pragma once

// The client's view of PlayerZoneClient::AllowedToAttack
// (game/mechanics/attack_rules.h, vector-checked) for the spawns it knows:
// CanIHit's melee check (spell -1) and the cast checks.
//
// What the client cannot know is taken as follows (docs/unknowns.md
// "Abilities"): other spawns' BodyWet and InPvPArea are 0 (only the
// player's own BodyWet is known); EQWorldData::IsNewbieZone is false; a
// vehicle race (vtable +0x50, the race data's flag) is a boat race (72, 73,
// 141); FUN_0059f730 (a target's PcClient owner) is non-null for player
// spawns; the level-capped character flag is never set.

#include <cstdint>
#include <functional>
#include <string>

#include "game/mechanics/attack_rules.h"

namespace nmsc::game {

struct WorldState;
class SpellTable;

struct AttackCheckInput {
    const WorldState* world = nullptr;
    const SpellTable* spells = nullptr;
    std::uint32_t target_id = 0;
    std::int32_t spell = mechanics::kMeleeAttack;
    std::uint8_t my_level = 0;       // the player's level (the profile's)
    mechanics::AttackRules rules;    // the server's PvP rule set (OP_NewZone / world) and the duel
    std::function<bool(const std::string& name)> self_or_group_member;
};

// AllowedToAttack(target, spell) with the local player as `self`.
bool allowed_to_attack(const AttackCheckInput& in);

} // namespace nmsc::game
