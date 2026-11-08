#pragma once

// Spell gems: memorising, the gem and recovery timers, linked timers and the gem count (checked against the
// original's vectors, and the exe's disassembly of CSpellBookWnd::GetSpellMemTicksLeft
// @0x75db20 and CharacterZoneClient::CanUseMemorizedSpellSlot @0x433bc0).
//
// Times: the gem timers (PlayerClient +0x2e8, 13 of them: gems 0..11 and 12 for items / AAs) and the recovery lock
// (+0x3cc) are absolute __EQGetTime ms (CDisplay +0x154 is the frame's copy), compared unsigned. The profile's
// spellSlotRefresh is ms remaining and becomes absolute on load. Linked timers are absolute Unix seconds
// (Util::FastTime), indices 1..19.
//
// Checked against CSpellBookWnd_GetSpellMemTicksLeft, CSpellBookWnd_FinishMemorizing, Handle_OP_MemorizeSpell,
// PcZoneClient_IsLinkedSpellTimerReady and CharacterZoneClient_CanUseMemorizedSpellSlot
// (tests/spell_gems_vectors_test.cpp).

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

namespace hailstone::game::mechanics::gems {

inline constexpr std::int32_t kMemorizeTicks = 92;   // StartSpellMemorization: 0x5c
inline constexpr std::int32_t kAaMemorizeBoost = 286; // HasAlternateAbility(0x11e): a bard's step doubles
inline constexpr std::int32_t kSpaSpellSlots = 326;   // SPA 0x146 SpellSlotIncrease
inline constexpr std::uint32_t kEqstrFinishedMemorizing = 12007; // 0x2ee7 "You have finished memorizing %1."
inline constexpr std::uint32_t kMemorizeColor = 264;   // 0x108
inline constexpr std::uint16_t kOpMemorizeSpell = 0x217c;

// ---- CSpellBookWnd::GetSpellMemTicksLeft @0x75db20 ----

// The book's memorise state (CSpellBookWnd +0x234 / +0x238 / +0x23c).
struct MemorizeBook {
    std::int32_t gem = -1;
    std::int32_t spell = -1;
    std::int32_t ticks_left = 0;
};

// OP_MemorizeSpell as the book sends it: {gem, spell, 1, ?}; the 4th dword comes from stack the original never
// writes (the client sends 0).
struct MemorizePacket {
    std::int32_t slot = 0;
    std::int32_t spell_id = 0;
    std::int32_t scribing = 1;
};

class MemorizeQueries {
public:
    virtual ~MemorizeQueries() = default;
    virtual std::uint8_t spell_level_needed(std::int32_t spell_id, std::uint8_t class_id) = 0; // GetSpellLevelNeeded
    virtual std::int32_t level() = 0;                         // CharacterBase vtable +0x68
    virtual std::int32_t calc_dex() = 0;                      // CalcDEX(1)
    virtual std::int32_t calc_int() = 0;                      // CalcINT(1)
    virtual std::int32_t adjusted_skill(std::int32_t skill) = 0; // GetAdjustedSkill
    virtual bool has_alternate_ability(std::int32_t aa) = 0;   // HasAlternateAbility(aa, 0, 0)
    virtual void push_pending_reason() = 0;                   // the reason 0x2a queued on CEverQuest +0x395b4
    virtual bool send(const MemorizePacket& p) = 0;           // UdpConnection::Send(4, 0x12 bytes)
    virtual void close_connection() = 0;                      // NetMgr_CloseConnection
    virtual void debug_log() = 0;                             // DebugLog_Printf (no world connection)
    virtual void cursor_attachment_vf94() = 0;                // pinstCCursorAttachment vtable +0x94
};

struct MemorizeWorld {
    std::uint8_t class_id = 0;      // the profile's +0x3374 (8 bard; NMS multiclass characters are bards)
    std::uint32_t now = 0;          // CDisplay +0x154
    std::uint32_t last_tick = 0;    // 0xf749a8 (0 after a memorise ends)
    std::int32_t item_pending = 0;  // EverQuestinfo::IsItemPending
    std::uint32_t pending_reasons = 0; // the reason vector's size (a reason is queued below 20)
    bool connected = true;          // __gWorld
    bool quiet_net_log = false;     // 0xddf654
};

// One gauge draw: a tick when now - last_tick > max(100, (level_needed - level + 5) * 100) (unsigned); each tick
// takes 3 + Meditate / 10 (Meditate at least 10) from 92, a bard (DEX + INT) / 30 + 1 doubled with AA 286, else plus
// Meditate / 10 + 2. At 0 OP_MemorizeSpell goes out, the book's gem and spell become -1 and last_tick 0. Returns the
// gauge, ticks_left * 1000 / 92 (0 when done or not memorising).
std::int32_t get_spell_mem_ticks_left(MemorizeBook& book, MemorizeWorld& w, MemorizeQueries& q);

// ---- CSpellBookWnd::FinishMemorizing @0x75c160 ----

class FinishQueries {
public:
    virtual ~FinishQueries() = default;
    virtual void set_memorized(std::uint32_t gem, std::int32_t spell_id) = 0; // profile +0x3060 + 4 x gem
    virtual void say(std::uint32_t eqstr, std::int32_t spell_id, std::uint32_t color) = 0; // the spell's name as %1
    virtual std::int32_t recast_time(std::int32_t spell_id) = 0; // EQ_Spell +0x18
    virtual void set_gem_timer(std::uint32_t gem, std::uint32_t ready) = 0; // PlayerClient +0x2e8 + 4 x gem
    virtual void hide_book() = 0;          // vtable +0x94
    virtual void continue_set_mem() = 0;   // CSpellBookWnd::ContinueSetMem
    virtual bool cast_window() = 0;        // pinstCCastSpellWnd
    virtual void update_spell_gems(std::int32_t gem) = 0; // CCastSpellWnd::UpdateSpellGems
};

// The server confirmed a memorise: the gem's ready time becomes now + RecastTime + 1000; a single memorise
// (mode 1) hides the book unless it was open before and ends, a spell set (mode 2) goes on, anything else refreshes
// the cast window's gems.
void finish_memorizing(std::int32_t& mode, bool keep_open, std::uint32_t gem, std::int32_t spell_id, std::uint32_t now,
                       FinishQueries& q);

// ---- Handle_OP_MemorizeSpell @0x4be150 ----

class MemorizeHandlerQueries {
public:
    virtual ~MemorizeHandlerQueries() = default;
    virtual void pop_pending_reason() = 0;
    virtual bool book_window() = 0;
    virtual bool cast_window() = 0;
    virtual void finish_scribing(std::int32_t book_slot, std::int32_t spell_id) = 0;
    virtual void finish_memorizing(std::int32_t gem, std::int32_t spell_id) = 0;
    virtual void forget_memorized_spell(std::int32_t gem) = 0;             // CCastSpellWnd::ForgetMemorizedSpell
    virtual bool local_pc() = 0;                                           // pinstLocalPC
    virtual bool local_player() = 0;                                       // pinstLocalPlayer
    virtual void set_memorized_spell(std::int32_t gem, std::int32_t spell_id) = 0; // CharacterBase_SetMemorizedSpell
    virtual bool is_cast_item_valid() = 0;                                 // LaunchSpellData_IsCastItemValid
    virtual void get_cast_item() = 0;                                      // LaunchSpellData_GetCastItem
    virtual std::int32_t item_cast_type() = 0;                             // PlayerClient +0x45c
    virtual std::int32_t item_recast_time(std::int32_t index) = 0;         // ItemBase_GetSpellRecastTime
    virtual std::uint32_t item_ready_time() = 0;                           // the cast item's +0x34
    virtual void set_item_ready_time(std::uint32_t ready, bool changed) = 0; // +0x34, +0x30 = 1 when it changed
    virtual bool is_valid_spell_id(std::int32_t id) = 0;
    // The record's RecastTime (+0x18) and RecoveryTime (+0x14); none without a record.
    virtual std::optional<std::pair<std::int32_t, std::int32_t>> spell_times(std::int32_t id) = 0;
    virtual std::int32_t total_effect(std::int32_t spa) = 0;               // TotalEffect(spa, 1, 0, 1, 1)
    virtual void set_gem_timer(std::int32_t gem, std::uint32_t ready) = 0;  // PlayerClient +0x2e8 + 4 x gem (signed)
    virtual void set_cast_slot(std::uint8_t slot) = 0;                      // +0x44c
    virtual void set_recovery(std::uint32_t ready) = 0;                     // +0x3cc
    virtual std::uint8_t class_id() = 0;                                    // the profile's +0x3374
    virtual std::int32_t next_bard_twist_gem() = 0;                         // PlayerClient_GetNextBardTwistSpell
    virtual void stop_current_spell_cast(std::int32_t reason) = 0;
    virtual void cast_gem(std::int32_t gem) = 0;                            // CCastSpellWnd::HandleSpellLeftClick
};

// OP_MemorizeSpell {slot, spell, scribing, reduction} from the server: -1 refused, 0 scribed, 1 memorised,
// 2 forgotten (each answers a pending request); 3 the refresh after a cast: the cast item's recast, then for gem
// <= 11 (signed) or 12 with a valid spell the gem timer now + RecastTime - reduction (gem 12: RecastTime - 1000 x
// SPA 295), and for gems <= 11 the casting gem 0xff, the recovery lock now + RecoveryTime (now for a bard) and a
// twisting bard's next song. Always returns 1.
std::int32_t handle_op_memorize_spell(std::int32_t scribing, std::int32_t slot, std::int32_t spell_id,
                                      std::int32_t reduction, std::uint32_t now, std::int32_t& item_pending,
                                      MemorizeHandlerQueries& q);

// ---- linked timers ----

// PcZoneClient_IsLinkedSpellTimerReady @0x57f920: false without a record (`reuse_index` none); true for an index
// outside 1..19; else whether the linked timer's ready time (Unix seconds) is at or before now (unsigned).
bool is_linked_spell_timer_ready(std::optional<std::int32_t> reuse_index, std::uint32_t now_unix,
                                 const std::function<std::uint32_t(std::int32_t)>& linked_timer);
// PcZoneClient_SetLinkedSpellReuseTimer @0x57f8e0: indices above 19 (unsigned) are ignored.
void set_linked_spell_reuse_timer(std::array<std::uint32_t, 20>& timers, std::uint32_t index, std::uint32_t ready);
// OP_LinkedReuse {id, end, start} @0x4cf2eb: FastTime() - start + end.
inline std::uint32_t linked_reuse_ready(std::uint32_t now_unix, std::uint32_t end, std::uint32_t start) {
    return now_unix - start + end;
}

// ---- CharacterZoneClient::CanUseMemorizedSpellSlot @0x433bc0 ----

struct SpellSlotTotals {
    std::function<std::int32_t()> aa, buff, item; // GetCachedAAEffect / Buff / Item (SPA 326, 0), in that order
};
// Without a profile, or with its +0x340c set, every gem; else gems < 8, and gem g >= 8 while g - 8 < the SPA 326
// total.
bool can_use_memorized_spell_slot(bool has_profile, std::int32_t profile_340c, std::int32_t gem,
                                  const SpellSlotTotals& totals);

// ---- gem readiness ----

// CCastSpellWnd::RefreshSpellGemButtons @0x647ea0 (the tint): not casting, a valid spell, !(now < gem timer),
// !(now < recovery) and the linked timer ready.
inline bool gem_ready_for_display(bool casting, bool has_spell, std::uint32_t now, std::uint32_t gem_timer,
                                  std::uint32_t recovery, bool linked_ready) {
    return !casting && has_spell && !(now < gem_timer) && !(now < recovery) && linked_ready;
}
// CCastSpellWnd::HandleSpellLeftClick @0x6488f0 (the click): now > gem timer and now > recovery (unsigned).
inline bool gem_castable(std::uint32_t now, std::uint32_t gem_timer, std::uint32_t recovery) {
    return now > gem_timer && now > recovery;
}

} // namespace hailstone::game::mechanics::gems
