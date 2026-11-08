#pragma once

// Opening and closing the spell book (CSpellBookWnd): follows the
// original's AboutToShow and OnShow, checked against
// CSpellBookWnd_AboutToShow.jsonl and CSpellBookWnd_OnShow.jsonl.
//  - AboutToShow @0x75c970: refuses a non-caster or a profile with +0x340c
//    set; a controlled spawn still moving (SpeedRun > 0) refuses with 12085;
//    looting, dead or feigning refuse silently. Otherwise a sitting player is
//    noted (the book did not sit it), anyone else sits down and auto attack
//    (12371) and auto fire (279) go off, colour 13.
//  - OnShow @0x75b6e0, hidden: a sitting player stands up when the book sat
//    it down.
// Closing triggers: a stand-state change away from sitting
// (PlayerZoneClient::ChangePosition -> LocalPlayer_CloseSpellbookAndDrag),
// horizontal movement (PlayerAnimation_SelectMovementAnim), GetConscious and
// MoveToZone. GameConnection keeps the state (open_spell_book /
// close_spell_book).

#include <cstdint>
#include <optional>
#include <vector>

namespace hailstone::game {

// StandState values (PlayerClient +0x35c): the book refuses 105 (looting), 115 (dead or feigned) and 102.
inline constexpr std::uint8_t kStandStanding = 100, kStandSitting = 110, kStandLooting = 105, kStandDead = 115,
                              kStandFrozen = 102;
inline constexpr std::uint32_t kStrMustBeStopped = 12085, kStrAutoAttackOff = 12371, kStrAutoFireOff = 279;

// What the original calls, in order.
struct SpellBookCall {
    enum class Kind : std::uint8_t {
        Chat,             // dsp_chat(text, color)
        Chat2,            // dsp_chat_2(text) (default colour)
        CastWindowHidden, // 0x646780 (only with a cast window)
        CastWindowShown,  // 0x646720 (no null check)
        ChangePosition,   // unk_SetStandState(state)
        SetAutoAttack,    // SetAutoAttack(0)
        SetAutoFire,      // SetAutoRangeAttack(0)
        ResetBookState,   // sub_0075c930
        AddGlobalFocus,
        SetFocus,
        TipWindowActivate, // CTipWnd::Activate(1, 0)
        SidlOnShow,
    };
    Kind kind{};
    std::uint32_t value = 0; // the text id or the stand state
    std::uint32_t color = 0;
};

struct SpellBookState {
    bool spellcaster = true;              // EQ_Character::IsSpellcaster != 0
    bool profile = true;                  // ProfileManager::GetCurrentProfile non-null
    std::int32_t profile_340c = 0;
    std::optional<float> controlled_speed; // pinstControlledPlayer's SpeedRun (+0x7c); none: no controlled spawn
    std::uint8_t stand_state = kStandStanding;
    bool auto_attack = false;             // __Attack
    bool auto_fire = false;               // __Autofire
    bool cast_window = true;              // pinstCCastSpellWnd non-null
    bool tip_window = false;              // pinstCTipWnd non-null
    bool was_sitting = false;             // CSpellBookWnd +0x248
};

// CSpellBookWnd::AboutToShow: true when the book opens; `s.was_sitting` is updated.
bool spell_book_about_to_show(SpellBookState& s, std::vector<SpellBookCall>& calls);
// CSpellBookWnd::OnShow: `shown` is the window's new state.
void spell_book_on_show(const SpellBookState& s, bool shown, std::vector<SpellBookCall>& calls);

} // namespace hailstone::game
