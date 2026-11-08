#pragma once

// CharacterZoneClient::CastSpell @0x43a830: every check the original makes
// before it sends OP_CastSpell, the cast it commits and the packet. Worked
// from the disassembly and checked against the original's own vectors
// (CastSpell_Checks / _Buffs / _Items / _Target /
// _Range / _BodyType / _Send; tests/cast_spell_vectors_test.cpp), which list
// every callee in order with its answer.
//
// The order, in short:
//  1. no spawn, or the local player under the lock 0xddf70c: 0, silently;
//  2. the spell (IsValidSpellID, the spell manager), for the local PC the
//     rank swap (CharacterBase virtual +0x90 off, group > 0, gem < 12: the
//     character's rank, virtual +0 (3), below the spell's: the spell of that
//     rank, GetSpellByGroupAndRank);
//  3. IsOkToTransact (asked twice; the first failure files report 0x3e; the
//     second refuses a spell cast: 12440, or 5819 for a skill);
//  4. not CastNotStanding: sitting stands up under the auto-stand option;
//     still not standing: 12441 / 300, feigned 12368; standing: SPA 40 above
//     0 (174), a stunned player (175 / 5820);
//  5. combat state (9190 / 9192, 9191 / 9194; 6460 / 6461), unconditional;
//  6. an item's recast timer (236);
//  7. target type 45 without a free-target tracker: the target indicator;
//  8. Sanctuary (SPA 312) removed unless the spell is beneficial on yourself
//     or needs no target and is type 6;
//  9. invisibility broken (MakeMeVisible(0xb0, 1)) unless the spell is a
//     sneak attack or CastNotStanding; a sneak attack needs hiding (9240);
// 10. an item cast: the item type 20, a bard's song in progress, the item's
//     level, attunement, must-equip (effect type 4), CanUseItem, charges,
//     SPA 25 under 0xddfc69 (105); a spell: already casting (12442, bards
//     may), gem 13..14 only for gems, the level (12443), SPA 32 with an item
//     on the cursor (3193);
// 11. SPA 152 on yourself (214 / 5825), spells 1502 / 2716 at no hit points
//     (263), the pet and owner retargets, the free target's line of sight
//     (108), line of sight (108), the mount's room (4067), the PvP deity rule
//     (210), bard state (212 / 213), PCNPCOnlyFlag (5864 / 5877), mana (199)
//     and endurance (5805);
// 12. the target-type switch: ranges (100 / 4602), the spell requirement
//     association (9171), body types, pets, corpses, group (196); then
//     +0x178 refused on yourself (210);
// 13. committed: the cast time (with the 50% floor), CastingData (above
//     100 ms), the gem timers, OP_CastSpell, the pending request counters.
// Messages shown "if loud" are suppressed for item casts (`item` set); a few
// are shown either way, as in the original.
//
// The objects are plain structs (fields at the original's offsets, named);
// every callee the original calls through a function or virtual pointer is a
// Queries call in the original's order. The readers of the spell record the
// original runs inline (EQ_Spell::SpellAffects, EQ_Spell_TargetTypeNeedsTarget,
// IsSneakAttack, IsSneakOrCastNotStanding, GetSpellLevelNeeded, the squared
// distance @0x442080, LaunchSpellData_Init on the packet) are ported here.
// Floats are kept as their bits; the x87 steps run at the FPU's precision
// (game/mechanics/fpu.h) and its comparisons treat NaN as unordered.

#include <array>
#include <cstdint>
#include <optional>

#include "game/mechanics/fpu.h"

namespace hailstone::game::mechanics::cast {

// The EQ_Spell fields CastSpell reads.
struct Spell {
    std::int32_t id = 0;              // +0x174
    std::uint32_t range = 0;          // +0x0 (float)
    std::uint32_t min_range = 0;      // +0x22c (float)
    std::uint32_t cast_time = 0;      // +0x10
    std::int32_t mana = 0;            // +0x28
    std::int32_t base0 = 0;           // +0x2c: Base[0]
    std::int32_t no_self = 0;         // +0x178: refused on yourself (210)
    std::int32_t endurance = 0;       // +0x194
    std::int32_t pcnpc_only = 0;      // +0x1cc: 1 players only, 2 NPCs only
    std::int32_t group = 0;           // +0x1d4 SpellGroup
    std::int32_t subgroup = 0;        // +0x1d8 SpellSubGroup
    std::int32_t rank = 0;            // +0x1dc SpellRank
    std::int32_t req_association = 0; // +0x1e8 SpellReqAssociationID
    std::uint8_t can_cast_in_combat = 1;     // +0x23c
    std::uint8_t can_cast_out_of_combat = 1; // +0x23d
    std::uint8_t only_during_fast_regen = 0; // +0x23f
    std::uint8_t cast_not_standing = 0;      // +0x240
    std::uint8_t is_skill = 0;               // +0x244
    std::array<std::uint8_t, 36> class_level{}; // +0x246 + class (1..35)
    std::uint8_t spell_type = 0;      // +0x26b: 0 detrimental, 1 beneficial, 2 group only
    std::uint8_t target_type = 0;     // +0x26e
    std::uint8_t travel_type = 0;     // +0x277 (target types 0 / 1 check their range only when 3)
    std::uint8_t sneak = 0;           // +0x4dc SneakAttack
    std::array<std::int32_t, 12> spa{}; // +0x11c (254: blank)
};

bool spell_affects(const Spell& s, std::int32_t spa);          // EQ_Spell::SpellAffects @0x4af770
std::uint8_t spell_level_needed(const Spell& s, std::uint32_t class_id); // EQ_Spell::GetSpellLevelNeeded @0x4af700
bool target_type_needs_target(std::uint8_t target_type);       // EQ_Spell_TargetTypeNeedsTarget @0x432500
// CharacterZoneClient::BardCastBard @0x432960: a bard (class 8) casting a spell that is not a skill and that bards
// can use (level 1..254). The live client answers Queries::bard_cast_bard with it.
bool bard_cast_bard(const Spell* s, std::uint32_t class_id);

// PlayerZoneClient::CastingData (LaunchSpellData, +0x444).
struct CastingData {
    std::uint32_t eta = 0;            // +0x444 SpellETA
    std::int32_t spell_id = -1;       // +0x448 (-1: not casting)
    std::uint8_t slot = 0;            // +0x44c
    std::array<std::uint32_t, 3> item_location{}; // +0x450
    std::uint32_t item_cast_type = 0; // +0x45c (a dword store)
    std::uint32_t item_id = 0;        // +0x460
    std::uint32_t y = 0, x = 0;       // +0x464 / +0x468 (floats)
    std::uint32_t target_id = 0;      // +0x470
    std::uint8_t free_target = 0;     // +0x477
    std::array<std::uint32_t, 3> target_position{}; // +0x478 (floats y, x, z)
};

// The PlayerClient fields CastSpell reads (and, for the caster, writes).
struct Spawn {
    std::uint8_t type = 0;            // +0x125: 0 player, 1 NPC, 2+ corpse
    std::uint32_t spawn_id = 0;       // +0x148
    std::uint8_t stand_state = 100;   // +0x35c: 100 standing, 110 sitting, 102 feigned
    std::uint32_t master_id = 0;      // +0x38c
    std::uint8_t mercenary = 0;       // +0x208
    std::uint8_t summoned = 0;        // +0x2e0 bSummoned
    std::int32_t hide_mode = 0;       // +0x338
    std::int32_t hp_max = 0;          // +0x2dc
    bool has_character = false;       // +0x2cc pCharacter != null
    std::uint32_t floor_height = 0;   // +0x28 (float)
    std::uint32_t ceiling = 0;        // +0x11b4 CeilingHeightAtCurrLocation (float)
    std::uint32_t height = 0;         // +0x138 AvatarHeight (float)
    std::uint32_t y = 0, x = 0, z = 0; // +0x64 / +0x68 / +0x6c (floats)
    CastingData casting;              // +0x444
    std::array<std::uint32_t, 13> gem_eta{}; // +0x2e8 SpellGemETA
    std::uint32_t cooldown_eta = 0;   // +0x3cc SpellCooldownETA
};

// An EQ_Affect as the Sanctuary scan reads it.
struct Affect {
    std::uint8_t active = 0;          // +0
    std::int32_t spell_id = 0;        // +8
};

// The cast item (VePointer<ItemClient>, argument 3).
struct Item {
    std::int32_t charges = 0;         // +0x44
    std::int32_t attuned = 0;         // +0x80
};

// A character object the callees know by address (a PcZoneClient, a spawn's
// PcClient): only passed on.
using Handle = std::uintptr_t;

// The OP_CastSpell payload (0x2c bytes after the opcode), as built at the
// frame's +0x3c. Bytes +0x1d..+0x1f are never written; the position is
// written when there is one, else zeroed by LaunchSpellData_Init on an item
// cast or a spell cast of at most 100 ms, else left unset (`position_set`).
struct Packet {
    std::uint32_t slot = 0;           // +0x00 (the gem byte)
    std::int32_t spell_id = 0;        // +0x04
    std::array<std::uint32_t, 3> item_location{}; // +0x08
    std::uint32_t target_id = 0;      // +0x14
    std::uint32_t crc = 0;            // +0x18
    std::uint8_t has_position = 0;    // +0x1c
    bool position_set = false;
    std::array<std::uint32_t, 3> position{}; // +0x20 y, x, z (floats, the argument's order)
    // The 44 payload bytes (unset bytes are 0).
    std::array<std::uint8_t, 0x2c> bytes() const;
};

// Every callee CastSpell calls through a pointer, in the original's terms.
class Queries {
public:
    virtual ~Queries() = default;
    virtual bool is_valid_spell_id(std::int32_t id) = 0;                       // IsValidSpellID @0x4ad150
    virtual const Spell* spell_by_id(std::int32_t id) = 0;                     // SpellManager vtable +0xc
    virtual bool character_virtual_90() = 0;                                   // CharacterBase vtable +0x90
    virtual std::int32_t character_rank(std::int32_t arg) = 0;                 // CharacterBase vtable +0 (3)
    virtual const Spell* spell_by_group_and_rank(std::int32_t group, std::int32_t subgroup, std::int32_t rank,
                                                 std::int32_t arg4) = 0;       // @0x5c1ae0
    virtual bool ok_to_transact() = 0;                                         // CEverQuest::IsOkToTransact
    virtual void pending_request_report(std::int32_t reason) = 0;              // PendingRequestLog_SendReport
    virtual void dsp_chat(std::uint32_t eqstr, std::int32_t color, std::uint8_t a3, std::uint8_t a4) = 0;
    virtual void change_position(Spawn& spawn, std::uint8_t state) = 0;        // @0x5a0bc0 (ChangePosition)
    virtual void message(std::uint32_t eqstr, std::int32_t color, std::int32_t arg4) = 0; // token_string_to_client
    virtual void recache_spell_effects() = 0;                                  // @0x438910
    virtual std::int32_t cached_buff_effect(std::int32_t spa, std::int32_t sub) = 0;
    virtual std::int32_t cached_aa_effect(std::int32_t spa, std::int32_t sub) = 0;
    virtual std::int32_t cached_item_effect(std::int32_t spa, std::int32_t sub) = 0;
    virtual Handle pc_zone() = 0;                                              // CharacterBase vtable +0x4c
    virtual bool in_combat() = 0;                                              // PcZoneClient_IsInCombat
    virtual bool resting_for_fast_regen() = 0;                                 // PcZoneClient_IsRestingForFastRegen
    virtual std::int32_t item_spell_recast_time(std::int32_t index) = 0;
    virtual std::int32_t item_recast_timer() = 0;                              // PcZoneClient::GetItemRecastTimer
    virtual std::int32_t fast_time() = 0;                                      // Util::FastTime
    virtual void create_target_indicator(std::uint8_t slot, const Spell& spell, std::int32_t item_cast_type) = 0;
    virtual std::uint8_t max_effects() = 0;                                    // GetMaxEffects
    virtual Affect* effect(std::int32_t index) = 0;                            // GetEffect
    virtual void remove_affect(Affect& affect, std::int32_t a2, std::int32_t a3) = 0;
    virtual void make_me_visible(std::int32_t a1, std::int32_t a2) = 0;
    virtual std::int32_t item_type() = 0;                                      // ItemClient::GetItemType
    virtual std::int32_t bard_cast_bard(const Spell* spell, std::uint8_t class_id) = 0;
    virtual std::int32_t item_spell_required_level(std::int32_t index) = 0;
    virtual std::int32_t character_level() = 0;                                // CharacterBase vtable +0x68
    virtual std::int32_t item_is_attuneable() = 0;
    virtual std::int32_t item_spell_effect_type(std::int32_t index) = 0;
    virtual const Item* inventory_item(std::int32_t slot) = 0;                 // unk_Container_GetItem
    virtual bool can_use_item(std::int32_t a2, std::uint8_t a3) = 0;           // CharacterZoneClient::CanUseItem
    virtual const Item* pc_item(std::int32_t slot) = 0;                        // the PcClient's GetItem (9: cursor)
    virtual std::int32_t character_virtual_34() = 0;                           // CharacterBase vtable +0x34
    virtual Spawn* pet_spawn() = 0;                                            // CharacterZoneClient_GetPetSpawn
    virtual Spawn* spawn_by_id(std::uint32_t id) = 0;                          // PlayerManagerClient::GetSpawnByID
    virtual bool can_see_position(Spawn& spawn, const std::array<std::uint32_t, 3>& position) = 0; // CanSee1
    virtual bool can_see(Spawn& spawn, Spawn& other) = 0;                      // PlayerBase::CanSee
    virtual bool spawn_virtual_dc(Spawn& spawn) = 0;                           // PlayerBase vtable +0xdc
    virtual std::int32_t not_on_same_deity(Spawn& spawn, Spawn& caster, const Spell& spell) = 0;
    virtual std::int32_t cur_mana(std::int32_t arg) = 0;
    virtual std::int32_t cur_endurance(std::int32_t arg) = 0;
    virtual bool spell_allowed(Spawn* target, std::int32_t association) = 0;   // SpellManager vtable +8
    virtual std::int32_t focus_range_modifier(const Spell& spell) = 0;         // GetFocusRangeModifier
    virtual std::uint32_t player_distance(Spawn& from, Spawn& to, std::uint32_t extra_bits) = 0; // float bits
    virtual Handle spawn_character_pc(Spawn& spawn) = 0;                       // 0x59f730
    virtual bool target_type_12_check(Spawn& spawn) = 0;                       // 0x59ed50
    // CharacterZoneClient_IsSelfOrGroupMember on `pc` for a name: the caster's character (who null) or a spawn's.
    virtual bool is_self_or_group_member(Handle pc, const Spawn* who) = 0;
    virtual bool has_property(Spawn& spawn, std::int32_t property, std::int32_t a2, std::int32_t a3) = 0;
    virtual std::int32_t item_base_type() = 0;                                 // ItemBase_GetType
    virtual std::int32_t item_spell_id(std::int32_t index) = 0;
    virtual std::int32_t largest_cached_effect(std::int32_t spa, std::int32_t sub) = 0;
    virtual std::int32_t casting_time_modifier(const Spell& spell) = 0;
    virtual std::int32_t focus_casting_time_modifier(const Spell& spell, std::int32_t arg3) = 0;
    virtual void start_casting(std::int32_t spell_id, std::uint16_t spawn_id, std::uint32_t cast_time) = 0;
    virtual void launch_spell_data_reset() = 0;                                // on the caster's CastingData
    virtual std::int32_t item_id() = 0;
    virtual std::int32_t item_spell_cast_time(std::int32_t index) = 0;
    virtual std::int32_t item_spell_recast_type(std::int32_t index) = 0;
    virtual void set_item_recast_timer(std::int32_t time, std::int32_t type) = 0;
    virtual std::uint32_t spell_crc(std::int32_t spell_id) = 0;                // SpellManager_GetSpellCRC
    virtual void hton(std::uint32_t opcode, std::int32_t arg2) = 0;           // CPacketScrambler::hton
    virtual bool send(std::int32_t channel, const Packet& packet, std::int32_t length) = 0; // UdpConnection::Send
    virtual void operator_delete(std::int32_t pointer) = 0;
    virtual void close_connection() = 0;                                       // NetMgr_CloseConnection
    virtual void debug_log(std::uint32_t opcode) = 0;                          // "send on a void connection"
    virtual void push_interact_reason(std::int32_t reason) = 0;
    virtual void push_pending_reason(std::int32_t reason) = 0;
};

// The call's arguments.
struct Args {
    std::uint8_t slot = 0;            // the gem (0..11), 12 an item; the original reads the low byte
    std::int32_t spell_id = 0;
    const Item* item = nullptr;       // argument 3: the cast item, null for a spell
    std::array<std::uint32_t, 3> item_location{}; // argument 4 (ItemGlobalIndex)
    std::int32_t item_cast_type = 0;  // argument 5
    std::uint8_t send_packet = 1;     // argument 6
    std::optional<std::array<std::uint32_t, 3>> position; // argument 9 (floats y, x, z)
};

// The state CastSpell reads and the globals it writes.
struct State {
    Spawn* caster = nullptr;          // this->me
    Spawn* local_player = nullptr;    // pinstLocalPlayer
    Spawn* target = nullptr;          // pinstTarget (CastSpell retargets it)
    bool is_local_pc = true;          // this == pinstLocalPC + 0x2dc8
    std::uint8_t local_block = 0;     // 0xddf70c
    std::uint8_t auto_stand = 0;      // 0xde0d5b
    std::uint8_t spa25_block = 0;     // 0xddfc69
    std::int32_t zone_type = 0;       // 0xdcee70 (4: the PvP deity rule)
    std::int32_t free_target_tracker = 0; // CEverQuest +0x5c0
    std::uint8_t quiet = 0;           // 0xddf654 (the offline switches; online both 0)
    std::int32_t demo = 0;            // 0xddf790
    bool connected = true;            // __gWorld
    std::uint32_t now = 0;            // CDisplay +0x154 TimeStamp
    // the character
    std::uint8_t effects_cached = 1;  // CharacterZoneClient +0x1a4
    std::uint8_t stunned = 0;         // CharacterBase +0xfc (1: stunned)
    std::uint8_t state_104 = 100;     // CharacterBase +0x104 (bards: 0x73 / 0x78 refuse a song)
    std::uint8_t skip_cost = 0;       // CharacterBase +0x460: no mana / endurance check
    std::uint8_t class_id = 0;        // the current profile's +0x3374
    // written
    std::optional<std::uint32_t> character_468;  // CharacterBase +0x468, cleared
    std::optional<std::uint32_t> last_beneficial; // 0xde0880 = spell +0x26b
    std::int32_t currently_interacting = 0;
    std::int32_t item_pending = 0;
    std::int32_t packet_count = 0;    // 0xf99584
    FpuPrecision precision = FpuPrecision::Single24;
};

// Runs CastSpell. Returns the original's al (1: the cast went ahead). The
// packet it sends goes to Queries::send (when connected); `sent` gets a copy.
std::uint8_t cast_spell(State& s, const Args& a, Queries& q, std::optional<Packet>* sent = nullptr);

} // namespace hailstone::game::mechanics::cast
