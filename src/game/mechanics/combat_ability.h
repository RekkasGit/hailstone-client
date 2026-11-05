#pragma once

// Combat abilities (disciplines): PcZoneClient::doCombatAbility @0x5808c0 from the exe's disassembly, and the
// rof2decomp ports of the checks it calls (re/src/game/character/combat_abilities.cpp,
// re/src/game/character/character_lookups.cpp) and of Cmd_discipline (re/src/game/commands/ability_commands.cpp).
//
// Times are Unix seconds (Util::FastTime); the reuse timers are the ready times (profile +0x69bc, ids 0..19).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nmsc::game::mechanics::combat_ability {

inline constexpr int kCombatAbilities = 300;             // PcBase::GetCombatAbility's slots (0 empty)
inline constexpr std::int32_t kTimers = 20;               // the profile's reuse timers 0..19
inline constexpr std::uint32_t kCastSlot = 13;            // CastSpell's slot for a combat ability
inline constexpr std::uint32_t kEqstrReadyHours = 5807;   // 0x16af "You can use the ability %1 again in %2 hour(s)..."
inline constexpr std::uint32_t kEqstrReadyMinutes = 5808; // 0x16b0 "... again in %2 minute(s) %3 seconds."
inline constexpr std::uint32_t kEqstrAlreadyActive = 5821; // 0x16bd "You are already using a discipline!"
inline constexpr std::uint32_t kEqstrWrongClass = 5840;   // 0x16d0 "This is a Warrior/Monk/... only ability."
inline constexpr std::int32_t kColor = 0x10f;

// The EQ_Spell fields these read.
struct Spell {
    std::int32_t id = 0;               // +0x174
    std::string name;                  // +0x27a
    std::uint32_t recast_time = 0;     // +0x18 ms
    std::int32_t duration_formula = 0; // +0x1c
    std::uint32_t deity_mask = 0;      // +0x15c (0: any deity)
    std::int32_t timer = 0;            // +0x198 CARecastTimerID
    std::int32_t group = 0;            // +0x1d4 SpellGroup
    std::int32_t subgroup = 0;         // +0x1d8 SpellSubGroup
    std::int32_t rank = 0;             // +0x1dc SpellRank
    std::uint8_t is_skill = 0;         // +0x244 (a combat ability)
    std::uint8_t target_type = 0;      // +0x26e
};

// Every callee, in the original's terms.
class Queries {
public:
    virtual ~Queries() = default;
    virtual const Spell* spell_by_id(std::int32_t id) = 0;                  // SpellManager vtable +0xc
    virtual const Spell* spell_by_group_and_rank(std::int32_t group, std::int32_t subgroup, std::int32_t rank,
                                                 std::int32_t arg4) = 0;    // @0x5c1ae0
    virtual std::int32_t combat_ability(std::int32_t index) = 0;            // PcBase::GetCombatAbility
    virtual bool character_virtual_90() = 0;                                // CharacterBase vtable +0x90
    virtual std::int32_t character_rank(std::int32_t arg) = 0;              // CharacterBase vtable +0 (3)
    virtual std::uint8_t spell_level_needed(const Spell& s, std::uint32_t class_id) = 0; // EQ_Spell::GetSpellLevelNeeded
    virtual std::uint32_t class_id() = 0;                                   // the profile's +0x3374
    virtual std::int32_t level() = 0;                                       // CharacterBase vtable +0x68
    virtual std::uint32_t deity() = 0;                                      // GetDeity
    // PlayerZoneClient +0x340 (AltAttack): when 1 it is cleared and the ability skips its checks.
    virtual bool take_alt_attack() = 0;
    virtual std::uint32_t combat_ability_timer(std::int32_t id) = 0;        // PcBase::GetCombatAbilityTimer (0..19)
    virtual std::uint32_t discipline_reuse_timer(std::int32_t id) = 0;      // GetDisciplineReuseTimer @0x57f810
    virtual std::uint32_t fast_time() = 0;                                  // Util::FastTime
    virtual bool discipline_active() = 0;                                   // PcZoneClient_IsDisciplineActive
    virtual void eqstr_template(std::uint32_t eqstr, std::int32_t color, const std::vector<std::string>& args) = 0;
    virtual void message(std::uint32_t eqstr, std::int32_t color) = 0;      // token_string_to_client
    virtual void dsp_chat_2(std::uint32_t eqstr) = 0;                       // CEverQuest::dsp_chat_2(GetString(eqstr))
    virtual void cast_spell(std::int32_t spell_id) = 0;                     // CastSpell(13, spell, no item, ...)
    // Cmd_discipline's call of PcZoneClient::doCombatAbility(spell, 1) through its address, which a server's patch
    // may detour (NMS: the reuse timers from 20 up); the default runs do_combat_ability below.
    virtual void use_combat_ability(std::int32_t spell_id);
};

// PcZoneClient::GetDeityBitmask @0x442490: 396 (agnostic) bit 0, deity 200 + n bit n (the shift mod 32).
std::uint32_t deity_bitmask(std::uint32_t deity);
// PcZoneClient::HasCombatAbility @0x57f850: one of the 300 is the spell, or (a spell with a group) one of the same
// group and subgroup at least its rank.
bool has_combat_ability(std::int32_t spell_id, Queries& q);
// PcZoneClient::CanUseMeleeCombatAbility @0x580730: 1 usable; 0 no such spell or not a combat ability, -1 not for
// the class, -2 above the level, -3 another deity, -4 a higher rank is known, -5 the rank is above the limit.
int can_use_melee_combat_ability(std::int32_t spell_id, Queries& q);
// PcZoneClient::IsCombatAbilityClass (Cmd_discipline's table): Warrior, Paladin, Ranger, Shadow Knight, Monk, Bard,
// Rogue, Beastlord, Berserker.
bool is_combat_ability_class(std::uint32_t class_id);

// PcZoneClient::doCombatAbility @0x5808c0 (spell, rank): with `rank` the spell becomes its group's rank at the
// character's limit when the limit is lower. Refuses (false) an unknown or unusable ability, one on its reuse timer
// (printing 5807 / 5808 with the time left) and a duration self spell while a discipline runs (5821); else casts it
// from slot 13 (true).
bool do_combat_ability(std::int32_t spell_id, bool rank, Queries& q);

// Cmd_discipline @0x4f1f00: for a combat ability class (else 5840), uses the first ability whose name starts with
// `args` (case-insensitive, the whole argument); without one lists the abilities still on their reuse timer.
void discipline_command(std::string_view args, Queries& q);

} // namespace nmsc::game::mechanics::combat_ability
