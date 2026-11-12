#pragma once

// The action numbers of OP_Animation and PlayerClient::PlayAnimation (spells' casting_anim and TargetAnim, the
// missile launch animations): the old-style numbering, whose clips are game::mechanics::old_anim_clip's. The
// locomotion numbers are game/mechanics/anim_select.h's OldStyleAnim and are taken from there; the other names
// describe what a classic model plays.

#include <cstdint>

#include "game/mechanics/anim_select.h"

namespace hailstone {

enum class AnimationType : std::uint8_t {
    Kick = 1,
    Piercing = 2,
    TwoHandSlash = 3,
    TwoHandBluntPiercing = 4,
    Throw = 5,
    OffhandAttack = 6,
    BashDragonPunch = 7,
    MainhandSlash = 8,
    ShootBow = 9,
    Swim = 10,
    RoundKick = 11,
    ShakeHeadNo_12 = 12,
    Embarrassed_13 = 13,
    Embarrassed_14 = 14,
    FallingBackwards = 15,
    Death = 16,
    Walk = game::mechanics::kOldWalk,
    Run = game::mechanics::kOldRun,
    JumpRun = game::mechanics::kOldJumpRun,
    JumpStand = game::mechanics::kOldJumpStand,
    Fall = game::mechanics::kOldFall,
    DuckWalk = game::mechanics::kOldDuckWalk,
    Climb = game::mechanics::kOldClimb,
    Duck = game::mechanics::kOldDuck,
    Tread = game::mechanics::kOldTread,
    Idle = game::mechanics::kOldIdle,
    Cheer = 27,
    Disgusted = 28,
    Wave = 29,
    Rude = 30,
    Yawn = 31,
    Stand = game::mechanics::kOldStand,
    SitStand = game::mechanics::kOldSitStand,
    Turn = game::mechanics::kOldTurn,
    NoEmote_35 = 35,
    Kneel = game::mechanics::kOldKneel,
    Swimming = game::mechanics::kOldSwim,
    Sit = game::mechanics::kOldSit,
    NoEmote_39 = 39,
    NoEmote_40 = 40,
    NoEmote_41 = 41,
    CastBuffs = 42,
    CastHeals = 43,
    CastNuke = 44,
    FlyingKick = 45,
    TigerClaw = 46,
    EagleStrike = 47,
    NodYes = 48,
    ShakeHeadNo_49 = 49,
    Plead = 50,
    ApplaudClap = 51,
    Unknown_52 = 52,
    Blush = 53,
    Chuckle = 54,
    Unknown_55 = 55,
    DuckEmote = 56,
    CuriousLook = 57,
    Dance = 58,
    Disagree = 59,
    Glare = 60,
    Peer = 61,
    Kneel_62 = 62,
    Laugh = 63,
    Point = 64,
    Shrug = 65,
    RaiseHand = 66,
    Salute = 67,
    Shiver = 68,
    TapFoot = 69,
    Bow_70 = 70,
    StandAfter = game::mechanics::kOldStandAfter,
    IdleAfter = game::mechanics::kOldIdleAfter,
    SitIdle = game::mechanics::kOldSitIdle,
};

// The animation a spell landing without an OP_BeginCast plays on its source (GameConnection's added proc cast
// stage): the spell's casting_anim. Hailstone (option cast_animation_fallback, on by default; docs/mechanics.md):
// a spell without one (0) plays CastHeals for a beneficial spell (spell_type above 0) and CastNuke for a
// detrimental one; off, the 0 as before (nothing plays). What the original plays for such a spell is not confirmed
// (docs/unknowns.md).
inline std::uint8_t spell_cast_animation(std::uint8_t casting_anim, std::uint8_t spell_type, bool fallback) {
    if (casting_anim != 0 || !fallback) return casting_anim;
    return static_cast<std::uint8_t>(spell_type > 0 ? AnimationType::CastHeals : AnimationType::CastNuke);
}

} // namespace hailstone
