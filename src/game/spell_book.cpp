#include "game/spell_book.h"

namespace nmsc::game {

namespace {
using K = SpellBookCall::Kind;
void call(std::vector<SpellBookCall>& c, K k, std::uint32_t v = 0, std::uint32_t color = 0) { c.push_back({k, v, color}); }
} // namespace

bool spell_book_about_to_show(SpellBookState& s, std::vector<SpellBookCall>& c) {
    if (!s.spellcaster) return false;
    if (!s.profile || s.profile_340c != 0) return false;
    if (s.controlled_speed && 0.0f < *s.controlled_speed) {
        call(c, K::Chat2, kStrMustBeStopped);
        if (s.cast_window) call(c, K::CastWindowHidden);
        return false;
    }
    if (s.stand_state == kStandLooting || s.stand_state == kStandDead || s.stand_state == kStandFrozen) {
        if (s.cast_window) call(c, K::CastWindowHidden);
        return false;
    }
    if (s.stand_state == kStandSitting) {
        s.was_sitting = true;
    } else {
        s.was_sitting = false;
        call(c, K::ChangePosition, kStandSitting);
        if (s.auto_attack) {
            call(c, K::SetAutoAttack);
            call(c, K::Chat, kStrAutoAttackOff, 0xd);
        }
        if (s.auto_fire) {
            call(c, K::SetAutoFire);
            call(c, K::Chat, kStrAutoFireOff, 0xd);
        }
    }
    call(c, K::ResetBookState);
    call(c, K::AddGlobalFocus);
    call(c, K::SetFocus);
    return true;
}

void spell_book_on_show(const SpellBookState& s, bool shown, std::vector<SpellBookCall>& c) {
    if (shown) {
        if (s.tip_window) call(c, K::TipWindowActivate);
        call(c, K::CastWindowShown);
        call(c, K::SidlOnShow);
        return;
    }
    if (s.cast_window) call(c, K::CastWindowHidden);
    if (s.stand_state == kStandSitting && !s.was_sitting) call(c, K::ChangePosition, kStandStanding);
    call(c, K::SidlOnShow);
}

} // namespace nmsc::game
