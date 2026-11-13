#pragma once

// The action numbers of OP_Animation and PlayerClient::PlayAnimation (spells' casting_anim and TargetAnim, the
// missile launch animations): the old-style numbering, whose clips are game::mechanics::old_anim_clip's. The
// locomotion numbers (Walk..Idle, Stand..Sit, StandAfter..SitIdle) are also the ones the classic model's movement
// selection (game/mechanics/anim_select.h) starts; the other names describe what a classic model plays.

#include <cstdint>

namespace hailstone {

enum class AnimationType : std::uint8_t {
    None = 0,
    Kick = 1,
    Piercing = 2,
    TwoHandSlash = 3,
    TwoHandBluntPiercing = 4,
    ThrowOrOneHandSlash = 5,   // C05: also one hand slashes (EQG SATK)
    OffhandOrOneHandSlash = 6, // C06: also one hand slashes
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
    Walk = 17,
    Run = 18,
    JumpRun = 19,
    JumpStand = 20,
    Fall = 21,
    DuckWalk = 22,
    Climb = 23,
    Duck = 24,
    Tread = 25,
    Idle = 26,
    Cheer = 27,
    Disgusted = 28,
    Wave = 29,
    Rude = 30,
    Yawn = 31,
    Stand = 32,
    SitStand = 33,
    Turn = 34,
    NoEmote_35 = 35,
    Kneel = 36,
    Swimming = 37,
    Sit = 38,
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
    StandAfter = 71,
    IdleAfter = 72,
    SitIdle = 73,
    Rider_74 = 74, // P09; 0x4a..0x4c and 0x4e are the rider animations (game/anim_layers.h)
    Rider_75 = 75, // L11
    Rider_76 = 76, // L12
    S29_77 = 77,
    Rider_78 = 78, // L10
    End = 79,      // 0x4f: one past the last old-style number (old_anim_clip is empty from it)
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
