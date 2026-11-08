#pragma once

// Melee timing, range and the attack itself as the original client runs them
// (rof2decomp batch 258, notes/combat-and-movement.md §2):
//  - __get_melee_range @0x4ddeb0 (with PlayerZoneClient_GetScaledMeleeRadius
//    @0x59f720): max(14, (radius_a * scale_a + radius_b * scale_b) * 0.75) +
//    2 + |AvatarHeight_a - AvatarHeight_b|, at most 75, each step stored as a
//    float;
//  - PlayerZoneClient::ModifyAttackSpeed @0x5a2c20: the delay after haste
//    (SPA 11, 100 = none) and a bow's weight-reduction quiver;
//  - PlayerZoneClient::GetHandToHandDelay @0x5a33f0;
//  - CharacterZoneClient_CheckDoubleAttack @0x44f390 and _CheckDualWield
//    @0x44f140 (the client's rolls only pick the swing animations: the server
//    runs its own melee rounds from OP_AutoAttack);
//  - PlayerZoneClient::CanIHit @0x5a2940 (with PlayerBase_DistanceSquared3D
//    @0x4dde50 and __get_bearing @0x4ef250), CanMeleeAttackNow @0x5a0600;
//  - PlayerZoneClient::DoAttack @0x5a5000 (slots 11 ranged, 13 primary, 14
//    secondary, 100 combat skill and 101 a strike; rof2decomp batch 269);
//  - the swing timers of CharacterZoneClient::DoPassageOfTime @0x45daf0: the
//    attack-on reset (@0x45db76) and the ready flags 0xde0864 / 0xde0865
//    (@0x45e3be..@0x45e804) that CEverQuest::ProcessControls (controls.h)
//    turns into DoAttack(13 / 14).
// The first five and DoAttack are checked against the original's vectors
// (MeleeRange, ModifyAttackSpeed, GetHandToHandDelay, CheckDoubleAttack,
// CheckDualWield, DoAttack call by call; tests/melee_vectors_test.cpp);
// CanIHit, CanMeleeAttackNow and the timers are ported from the disassembly
// (no vectors). Every callee the original calls through
// a pointer is a query, in the original's order; x87 steps run at the FPU's
// precision (fpu.h). __get_bearing's fpatan is the one step not emulated bit
// for bit: it runs in long double (the x87's 64-bit significand), so only a
// facing exactly on the 80 / 256 limit could come out differently.

#include <array>
#include <cstdint>
#include <optional>

#include "game/mechanics/fpu.h"

namespace hailstone::game::mechanics::melee {

// ---------------------------------------------------------------- __get_melee_range

// The spawn fields __get_melee_range reads.
struct RangeSpawn {
    float melee_radius = 0.0f; // PlayerClient +0x105c MeleeRadius
    float scale = 1.0f;        // +0x31c, the model scale
    float height = 0.0f;       // PlayerBase +0x138 AvatarHeight
};

// __get_melee_range @0x4ddeb0: 14 without both spawns.
float melee_range(const RangeSpawn* a, const RangeSpawn* b, const Fpu& fpu);

// ---------------------------------------------------------------- ModifyAttackSpeed

// The bag search of ModifyAttackSpeed (inventory slots 23..32).
class BagQueries {
public:
    virtual ~BagQueries() = default;
    virtual bool slot_available(std::int32_t slot) = 0;   // CharacterBase_IsInvSlotAvailable (its low byte)
    virtual bool has_item(std::int32_t slot) = 0;         // unk_Container_GetItem gave an item
    virtual std::int32_t item_class(std::int32_t slot) = 0; // ItemBase_GetType: 1 a container
    virtual std::int32_t container_type(std::int32_t slot) = 0; // ItemBase_GetContainerType: 2 a quiver
    virtual std::int32_t weight_reduction(std::int32_t slot) = 0; // ItemBase_GetWeightReduction
};

// PlayerZoneClient::ModifyAttackSpeed @0x5a2c20: a delay of 0 stays 0. With haste h = TotalEffect(11) above 0 the
// delay becomes trunc(delay / (h * 0.01)) (the delay read as unsigned; the low dword of the 64-bit result). With
// `with_bags` (the whole dword) the first bag slot 23..32 that is available and holds a container decides: a quiver
// (container type 2) with weight reduction above 0 takes ftol(wr * 0.0025 * delay) + 1 off, at least 700
// (unsigned); another container goes on to the next slot. `haste` is asked only for a delay that is not 0.
std::uint32_t modify_attack_speed(std::uint32_t delay, std::uint32_t with_bags, std::int32_t haste, BagQueries* bags,
                                  const Fpu& fpu);

// ---------------------------------------------------------------- GetHandToHandDelay

struct HandToHandInput {
    bool has_character = true;     // PlayerZoneClient +0x2cc
    std::int32_t skill = 0;        // the character's virtual +0x18 (28, 1): hand to hand with item mods
    bool hands_item = false;       // an item in the hands slot (12)
    std::int32_t hands_item_id = 0; // ItemBase_GetID: 10652 (Celestial Fists) above level 46 caps at 280
    std::int32_t level = 0;        // the virtual base's +0x68
    std::int32_t profile_3370 = 0; // the current profile's +0x3370: 0x80 adds one to the base
};
// PlayerZoneClient::GetHandToHandDelay @0x5a33f0: (base + 38 - min(cap, skill) / 21) * 100 with cap 280 for the
// epic (else 0) and base 1 when the profile's +0x3370 is 0x80 (and no epic); nothing without a character.
std::optional<std::int32_t> hand_to_hand_delay(const HandToHandInput& in);

// ---------------------------------------------------------------- the rolls

class RollQueries {
public:
    virtual ~RollQueries() = default;
    virtual std::int32_t adjusted_skill(std::int32_t skill) = 0; // CharacterZoneClient::GetAdjustedSkill
    virtual std::int32_t skill(std::int32_t skill) = 0;          // the character's virtual +0x18 (skill, 1)
    virtual std::int32_t level() = 0;                            // the virtual base's +0x68
    virtual std::int32_t total_effect(std::int32_t spa) = 0;     // TotalEffect(spa, 1, 0, 1, 1)
    virtual std::int32_t random() = 0;                           // Random(): the additive RNG & 0x3fffffff
};

// CharacterZoneClient_CheckDoubleAttack @0x44f390 (both arguments unused): monks and rangers (profile class 7, 4)
// never while their spawn's +0x364 (DoSpecialMelee) is set. The chance is skill 20 + level when GetAdjustedSkill(20)
// is above 0, else 0 and only with SPA 225; plus SPA 225 * 500 / 100, plus SPA 177 percent of it (32-bit wraps, C
// division); true when Random() % 500 + 1 is within it.
struct DoubleAttackInput {
    std::uint8_t class_id = 0;     // the current profile's +0x3374
    bool has_spawn = true;         // CharacterZoneClient +8
    std::int32_t do_special_melee = 0; // the spawn's +0x364
};
bool check_double_attack(const DoubleAttackInput& in, RollQueries& q);

// CharacterZoneClient_CheckDualWield @0x44f140: false without skill 22 (GetAdjustedSkill) or with a two-hander
// (ItemClient::GetItemType 1, 4 or 0x23 of an item whose ItemBase_GetType is 0) in the primary; otherwise
// Random() % 375 + 1 against level + (skill 22 + SPA 276), raised by SPA 176 percent.
struct DualWieldInput {
    bool primary = false;          // an item in slot 13
    std::int32_t primary_class = 0; // ItemBase_GetType
    std::int32_t primary_type = 0;  // ItemClient::GetItemType
};
bool check_dual_wield(const DualWieldInput& in, RollQueries& q);

// ---------------------------------------------------------------- the spawns and items DoAttack reads

struct Spawn {
    std::uint8_t type = 0;          // +0x125: 0 player, 1 NPC, 2+ corpse
    float y = 0.0f, x = 0.0f, z = 0.0f; // +0x64 / +0x68 / +0x6c
    float heading = 0.0f;           // +0x80
    float height = 0.0f;            // +0x138 AvatarHeight
    float melee_radius = 0.0f;      // +0x105c
    float scale = 1.0f;             // +0x31c
    bool has_actor = true;          // +0x101c: CanIHit refuses (silently) without the actor on either side
    std::uint8_t body_wet = 0;      // +0xa2: DoAttack refuses a target in the other medium (token 123)
    std::uint8_t always_hit = 0;    // +0x251: a target with it set is swung at even before the delay is up
    std::uint8_t stand_state = 100; // +0x35c
    std::int32_t is_attacking = 0;  // +0x21c (an NPC's)
    std::int32_t do_special_melee = 0; // +0x364
    RangeSpawn range() const { return {melee_radius, scale, height}; }
};

// An item reference as DoAttack and DoPassageOfTime hand it around (the VePointer an unk_Container_GetItem or
// AttackWeaponRef_GetItem lookup fills): opaque to the port, answered by the queries. kNoItem is a null pointer,
// kFistItem the fist pseudo-item (the ItemClient at 0xe6427c, whose definition is FistDefinition).
using ItemRef = std::int32_t;
inline constexpr ItemRef kNoItem = -1, kFistItem = -2;

// The fist's ItemDefinition (0xe64260, built at 0x599ef2): the fields DoAttack writes before a swing with it.
struct FistDefinition {
    bool present = true;           // the global pointer is set (slot 100 refuses without it)
    std::int32_t damage = 0;       // +0x188 Damage
    std::int32_t damage_copy = 0;  // +0x18c
    std::uint8_t delay = 0x1c;     // +0x182 Delay: the fist's holds a skill (28 hand to hand; slot 100 its skill)
    std::uint8_t item_type = 0x16; // +0x1d4 ItemType: 0x16 hand to hand
};

// The current profile's fields DoAttack reads (ProfileManager::GetCurrentProfile).
struct AttackProfile {
    std::int32_t fist_damage = 0;  // +0x346c: the fist's damage (else GetSkillBaseDamage(28))
    std::int32_t fist_delay = 0;   // +0x3470: a hand-to-hand fist's delay when above 0
    std::int32_t double_attack = 0; // +0x30f0 Skill[20]: above 149 the secondary may double attack
};

// ---------------------------------------------------------------- CanIHit

// What CanIHit and CanMeleeAttackNow ask and tell.
class HitQueries {
public:
    virtual ~HitQueries() = default;
    virtual bool has_character() = 0;                 // PlayerZoneClient +0x2cc
    virtual std::int32_t total_effect(std::int32_t spa) = 0; // TotalEffect(spa, 1, 0, 1, 1)
    // StringTable::getString(id) + CEverQuest::dsp_chat(text, color, 1, 1).
    virtual void chat(std::uint32_t eqstr, std::int32_t color) = 0;
    // token_string_to_client(character, id, color, 0).
    virtual void token(std::uint32_t id, std::int32_t color) = 0;
    virtual bool allowed_to_attack(const Spawn& target) = 0; // PlayerZoneClient::AllowedToAttack(target, -1)
    virtual bool can_see(const Spawn& target) = 0;           // PlayerBase::CanSee
};

// PlayerBase_DistanceSquared3D @0x4dde50: (dy * dy + dx * dx) + dz * dz with the feet (z - AvatarHeight); unrounded.
double distance_squared_3d(const Spawn& a, const Spawn& b, const Fpu& fpu);
// __get_bearing @0x4ef250 (y1, x1, y2, x2): the heading from the first point toward the second, 0..511.5;
// unrounded.
double bearing(float y1, float x1, float y2, float x2, const Fpu& fpu);

// PlayerZoneClient::CanIHit @0x5a2940: SPA 40 (12695), the target itself (12696), none (12697); AllowedToAttack;
// both actors; the range (melee range, x1.25 PC vs PC when extra is 0, plus extra) squared against
// DistanceSquared3D (token 124; NaN passes); the facing (|bearing - heading| at most 80, 256 PC vs PC, with the
// wraps at 472/40 and 350/150; token 108); CanSee (token 123).
bool can_i_hit(const Spawn& me, const Spawn* target, float extra, HitQueries& q, const Fpu& fpu);

// PlayerZoneClient::CanMeleeAttackNow @0x5a0600.
struct MeleeNowInput {
    bool casting = false;          // CastingData +0x448 != -1
    std::uint8_t class_id = 0;     // +0xeb8: bards (8) may while casting
    bool has_character = true;
    std::uint8_t type = 0;
    std::uint8_t char_state = 0;   // CharacterBase +0x100: 1 stunned (token 125), 2..4 refuse silently
    bool corpse = false;           // PlayerClient::IsCorpse
    std::uint8_t pc_state = 0;     // the PcClient's character +0x104: 4 or 7 refuse
    bool pc_vf60 = false;          // the PcClient's virtual +0x60 (refuses when true)
};
bool can_melee_attack_now(const MeleeNowInput& in, HitQueries& q);

// ---------------------------------------------------------------- DoAttack

// The PlayerZoneClient fields DoAttack and DoPassageOfTime keep, the fist's definition and the two ready flags.
struct AttackState {
    std::uint32_t last_primary = 0;   // +0x23c LastPrimaryUseTime (also the ranged slot's timer in DoAttack)
    std::uint32_t last_secondary = 0; // +0x218 LastSecondaryUseTime
    std::uint32_t last_ranged = 0;    // +0x230 LastRangedUsedTime (reset with the others)
    std::uint8_t last_attack = 0;     // +0x238 LastAttack: the last slot swung (11 picks the ranged delay)
    // +0x368 CombatSkillTicks (the reuse a combat skill set) and +0x390 CombatSkillUsed (when it was used), indexed by
    // GetCombatSkillTimerIndex. eqlib declares two of each and the index is 0 or 1 (Tiger Claw); DoAttack does not
    // bound it, so four are kept for the vectors' indexes 2 and 3 (the next two dwords of each).
    std::array<std::uint32_t, 4> combat_ticks{};
    std::array<std::uint32_t, 4> combat_used{};
    std::uint32_t last_intimidate = 0; // +0x188 LastIntimidateUse (GetTickCount)
    std::uint32_t alt_attack = 0;     // +0x340 AltAttack: slot 100 skips the skill timer; every slot-100 exit clears it
    FistDefinition fist;              // 0xe64260
    bool primary_ready = false;       // 0xde0864
    bool secondary_ready = false;     // 0xde0865
};

// What DoAttack asks and tells, one call per callee of the original (in its order). Byte results are the original's
// al. HitQueries::chat is StringTable::getString + CEverQuest::dsp_chat(text, color, 1, 1), token is
// token_string_to_client(character, id, color, 0).
class AttackQueries : public HitQueries {
public:
    virtual std::uint32_t tick_count() = 0;            // GetTickCount (intimidation's reuse)
    virtual std::uint32_t now() = 0;                   // __EQGetTime (AttackTimer_Sample)
    virtual bool attack_blocked() = 0;                 // the byte 0xddf70c (read, not a call)
    virtual std::uint8_t is_player_spawn() = 0;        // IsSpawnInfoPlayerSpawn(this)
    virtual std::uint8_t can_melee_attack_now(const Spawn* target) = 0; // PlayerZoneClient::CanMeleeAttackNow
    // The Sanctuary loop (@0x5a50dc): GetPCSpellAffect(312, 0, 0) (an affect, 0 for none) / RemovePCAffectex(it, 0).
    virtual std::uint32_t spell_affect(std::int32_t spa) = 0;
    virtual void remove_affect(std::uint32_t affect) = 0;
    virtual void vf_c8() = 0;                          // PlayerZoneClient virtual +0xc8 (meaning not traced)
    virtual std::uint8_t can_use_combat_skill(std::int32_t slot, std::int32_t skill) = 0; // CanUseCombatSkill
    virtual ItemRef item(std::int32_t slot) = 0;       // unk_Container_GetItem(slot)
    // AttackWeaponRef_GetItem (+ _Wrapper, the same lookup): the fist when `fist`, else the slot's item.
    virtual ItemRef weapon_ref_item(std::int32_t slot, bool fist, bool wrapper) = 0;
    virtual std::int32_t id_file(ItemRef item) = 0;    // ItemBase_GetIDFile(0x22): the item's model (an opaque id)
    // The model owner (virtual +0x44)'s PlayerClient::SetHeldModel(slot 7 / 8, model, 0, 1); kNoModel is "0".
    virtual void set_held_model(std::int32_t slot, std::int32_t model) = 0;
    virtual bool auto_fire() = 0;                      // the byte 0xe635ab
    virtual void set_auto_range_attack(bool on) = 0;   // SetAutoRangeAttack
    virtual std::int32_t base_damage(std::uint32_t skill, const Spawn* target) = 0; // GetSkillBaseDamage
    virtual void fist_set_definition() = 0;            // the fist ItemClient's virtual +0xc(0xe64260)
    virtual std::int32_t item_type(ItemRef item) = 0;  // ItemClient::GetItemType
    virtual std::uint8_t is_weapon(ItemRef item) = 0;  // ItemBase_IsWeapon
    // ItemBase_GetDelay @0x7b06d0 (ItemDefinition +0x182 Delay): a weapon's delay in tenths of a second; a
    // hand-to-hand (type 0x16) item, the fist above all, keeps a skill there instead (rof2decomp's rewrite and
    // vectors call it the item's skill, get_item_skill).
    virtual std::uint32_t item_delay(ItemRef item) = 0;
    virtual std::int32_t item_range(ItemRef item) = 0; // ItemBase_GetRange(1)
    virtual std::int32_t item_stack(ItemRef item) = 0; // the ItemClient's +0x98 (its stack count)
    virtual std::int32_t item_id(ItemRef item) = 0;    // ItemBase_GetID
    virtual std::uint8_t can_use_item(ItemRef item) = 0; // CharacterZoneClient_CanUseItem(&item, 1, 0)
    virtual AttackProfile profile() = 0;               // ProfileManager::GetCurrentProfile
    virtual std::int32_t reuse_time(std::uint32_t skill) = 0; // CSkillMgr::GetReuseTime
    // PlayerZoneClient::GetHandToHandDelay(&delay, hands item): writes `delay` only with a character.
    virtual void hand_to_hand_delay(std::int32_t& delay, ItemRef hands) = 0;
    virtual std::uint32_t modify_attack_speed(std::uint32_t delay, bool archery) = 0; // ModifyAttackSpeed
    virtual std::uint32_t combat_timer_index(std::uint32_t skill) = 0; // GetCombatSkillTimerIndex
    virtual std::uint32_t skill_reuse_time(std::uint32_t skill) = 0; // CharacterZoneClient_GetSkillReuseTime
    virtual void set_skill_timer_duration(std::uint32_t skill, std::uint32_t ms) = 0; // CSkillMgr
    virtual void chat2(std::uint32_t eqstr) = 0;       // StringTable::getString + CEverQuest::dsp_chat_2
    // CDisplay::PlayerDistance(this, target, 0): the x87's st0 (unrounded beyond the precision).
    virtual double player_distance(const Spawn* target) = 0;
    virtual bool hit_anyway() = 0;                     // the byte 0xddf654: CanIHit failures are ignored
    virtual std::uint8_t can_i_hit(const Spawn* target, float range) = 0; // PlayerZoneClient::CanIHit
    virtual void debug_cant_hit() = 0;                 // NullDebugPrintf("... I CAN'T HIT %s.")
    // SendCombatAbilityAttack(this, target, &item, slot, skill): slots 13 / 14 swing (the animation only), 100 sends
    // OP_CombatAbility.
    virtual void send_attack(const Spawn* target, ItemRef item, std::int32_t slot, std::uint32_t skill) = 0;
    virtual std::uint8_t check_double_attack(const Spawn* target, std::int32_t slot) = 0; // _CheckDoubleAttack
    virtual std::uint8_t check_dual_wield(const Spawn* target) = 0; // CharacterZoneClient_CheckDualWield
    virtual void ranged_attack(const Spawn* target) = 0; // PlayerZoneClient::DoRangedAttack
    // Not a callee: a server extension's switch (game/server_ext.h backstab_either_hand). True: the Backstab
    // check also takes a piercer in the secondary.
    virtual bool backstab_either_hand() { return false; }
};
inline constexpr std::int32_t kNoModel = -1;

// PlayerZoneClient::DoAttack @0x5a5000 for `me` (its +0x125 type, +0x35c stand state, +0x21c, +0x364 and +0xa2 are
// read; +0x21c is written). Slots 13 primary / 14 secondary / 11 range swing a weapon or the fist; 100 strikes with
// a combat skill through the fist, 101 with the primary (or the fist). `skill` is the argument as passed: the
// original compares and forwards its low byte, and hands the whole dword to GetSkillBaseDamage (slot 100); `slot`
// is read as a byte. Returns the original's al: 1 also when it was too early or there was no weapon to swing.
// rof2decomp batch 269's vectors (DoAttack, 1200) check it call by call.
bool do_attack(AttackState& s, Spawn& me, const Spawn* target, std::uint32_t slot, std::uint32_t skill,
               AttackQueries& q, const Fpu& fpu);

// ---------------------------------------------------------------- ranged attacks

// PlayerZoneClient::HandleAmmo @0x5a35b0 (rof2decomp re/src/game/spawn/ammo_eh.cpp): 0 throw the range item, 1
// shoot the ammo, 2 neither (or the thrown stack, see do_ranged_attack). A range item not of type 0x13 (thrown), 5
// (bow) or 6 gives 0 with a range, else 2. Without ammo: a thrown stack above 1 gives 2 for a player (+0x125 0), a
// single one 0. With ammo: a thrown range item whose ammo is the same item gives whether the ammo's stack is above
// 0; a bow with arrows (0x1b) or type 6 with 0x1c, stacked, gives 1; a thrown range item with any stacked ammo 2.
// Anything else prints eqstr 12875 (0x324b, "You have run out of ammo!!", dsp_chat_2) and gives 2.
std::uint8_t handle_ammo(const Spawn& me, AttackQueries& q);
// PlayerZoneClient::DoRangedAttack @0x5a4920 (rof2decomp re/src/game/spawn/ranged_attack_eh.cpp): false without a
// range item. By HandleAmmo: 0 throws the range item (SendCombatAbilityAttack(target, range item, 11, -1), model 8
// "0"); 1 shoots the ammo (the attack with the ammo, model 7 "0", model 8 the range item's, else the primary's
// (slot 14 in the rewrite's naming), else "0"); 2 throws a type 0x13 range item (model 7 from slot 13, model 8 as
// for 1). Without a target false (after the ammo check); otherwise true.
bool do_ranged_attack(const Spawn& me, const Spawn* target, AttackQueries& q);

// ---------------------------------------------------------------- DoPassageOfTime's swing timers

// @0x45db76: while auto attack is on and the primary flag is still set, both flags are cleared and the three
// timers restart (so the first swing after turning attack on waits a whole delay).
void attack_on_reset(AttackState& s, bool attack_on, AttackQueries& q);

// @0x45e3be..@0x45e804 (`now` is DoPassageOfTime's __EQGetTime): the primary's delay (ModifyAttackSpeed of the
// slot-13 weapon's delay * 100, or a hand-to-hand weapon's GetReuseTime(delay); an item with delay 0 or one the
// player cannot use is the fist; 0 when the slot holds no attack weapon), the ranged slot's (a bow's quiver
// counts), the ranged delay in place of the primary's after a ranged attack (+0x238 == 11); then the primary flag
// is set when now - +0x23c reaches it, and the secondary flag when now - +0x218 reaches the slot-14 delay (the
// primary's when slot 14 holds no attack weapon).
void swing_ready(AttackState& s, std::uint32_t now, AttackQueries& q);

} // namespace hailstone::game::mechanics::melee
