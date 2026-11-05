#pragma once

// Who may attack, or cast a spell on, whom: PlayerZoneClient::AllowedToAttack
// @0x5a2360 and PlayerZoneClient::NotOnSameDeity @0x59e8d0 (the PvP rules).
//
// Exact port of rof2decomp's rewrite (re/src/game/spawn/attack_rules.cpp),
// checked against PlayerZoneClient_AllowedToAttack.jsonl (batch 252), whose
// records list every callee's answer in call order. The original asks its
// callees (the attacker virtual, levels, masters, spells, deities, the group)
// through AttackQueries, in its order.

#include <cstdint>

namespace nmsc::game::mechanics {

struct AttackCharacter;

// The spawn fields the rules read (PlayerZoneClient offsets).
struct AttackSpawn {
    std::uint8_t type = 0;         // +0x125: 0 player, 1 NPC, 2/3 corpses
    std::uint32_t id = 0;          // +0x148
    std::uint32_t master_id = 0;   // +0x38c
    std::uint8_t body_wet = 0;     // +0xa2
    bool in_pvp_area = false;      // +0x2e1
    bool pvp = false;              // +0x349 PvPFlag
    std::int32_t guild_id = -1;    // +0x34c
    std::uint32_t zone = 0;        // +0x210
    std::int32_t level_cap = 0;    // +0x258
    std::uint32_t deity = 0;       // +0x518
    const AttackCharacter* character = nullptr; // +0x2cc
};

// The spawn's character: its level-capped flag (virtual base +0x464) and its spawn (+8).
struct AttackCharacter {
    bool level_capped = false;
    const AttackSpawn* spawn = nullptr;
};

struct AttackSpell {
    std::int32_t target_type = 0; // EQ_Spell +0x11c
    std::uint8_t beneficial = 0;  // EQ_Spell +0x26b (0 detrimental, 1 beneficial, 2+ group/self rules)
};

// The server's rule set and the duel state.
struct AttackRules {
    std::int32_t pvp_rules = 0;       // 0xdcee70 (2: 8 levels; 4: deity; 6: no level range)
    bool dueling = false;             // 0xddf7ec
    std::uint32_t duel_target_id = 0; // 0xddf7e4
};

class AttackQueries {
public:
    virtual ~AttackQueries() = default;
    virtual const AttackSpawn* attacker_of(const AttackSpawn& self) = 0;   // vtable +0x44
    virtual std::uint8_t level(const AttackSpawn& s) = 0;                  // PlayerZoneClient::GetLevel
    virtual std::uint32_t top_master_id(const AttackSpawn& s) = 0;         // PlayerClient_GetTopMasterSpawnID
    virtual bool is_vehicle_race(const AttackSpawn& s) = 0;                // vtable +0x50
    virtual bool is_gm(const AttackSpawn& s) = 0;                          // vtable +0xdc
    virtual bool is_valid_spell_id(std::int32_t id) = 0;                   // IsValidSpellID
    virtual const AttackSpell* spell_by_id(std::int32_t id) = 0;           // the spell manager's vtable +0xc
    virtual bool spell_affects(const AttackSpell& s, std::int32_t attribute) = 0; // EQ_Spell::SpellAffects
    virtual const AttackSpawn* spawn_by_id(std::uint32_t id) = 0;          // PlayerManagerClient::GetSpawnByID
    virtual bool not_on_same_deity(const AttackSpawn& self, const AttackSpawn& other, const AttackSpell* spell) = 0;
    virtual bool has_owner(const AttackSpawn& target) = 0;                 // FUN_0059f730 non-null
    virtual bool is_corpse(const AttackSpawn& s) = 0;                      // PlayerClient::IsCorpse
    virtual bool is_self_or_group_member(const AttackSpawn& s) = 0;        // CharacterZoneClient_IsSelfOrGroupMember
    virtual bool is_newbie_zone(std::uint32_t zone) = 0;                   // EQWorldData::IsNewbieZone
};

inline constexpr std::int32_t kMeleeAttack = -1;

// PlayerZoneClient::AllowedToAttack(target, spell): `self` is the local
// player's PlayerZoneClient; spell -1 is melee.
bool allowed_to_attack(const AttackSpawn& self, const AttackSpawn* target, std::int32_t spell, const AttackRules& rules,
                       AttackQueries& q);

// PlayerZoneClient::NotOnSameDeity: 1 unless the deities (201..216) are in
// the same alignment group; with no spell a GM target (`other_is_gm`) is on
// the same side.
bool not_on_same_deity(const AttackSpawn* self, const AttackSpawn* other, bool spell, bool other_is_gm);

} // namespace nmsc::game::mechanics
