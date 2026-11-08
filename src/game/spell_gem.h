#pragma once

// A spell gem's look, as CSpellGemWnd draws it (rof2decomp batch 256: answered from the disassembly of
// CSpellGemWnd::Draw @0x760b70, with CButtonWnd's cool-down helpers @0x8970a0..0x897a30):
//
//  - SetSpellGemTint @0x760200: eleven tints, each darker by an eleventh of every channel (truncated), with icon
//    alphas 0xff, 0xea, ... 0x2d. The background is drawn in SpellGemTintArray[TintIndex] (+0x202020 under the
//    mouse, white while checked), the icon at SpellGemAlphaArray[TintIndex] (opaque white at index 0).
//  - CCastSpellWnd::RefreshSpellGemButtons @0x647ea0 sets each gem's stage (SetGemTintStage @0x760330): 1 while it
//    cannot be cast (a spell being cast, no spell, its SpellGemETA or the global SpellCooldownETA not reached, its
//    linked timer running), 2 once ready after stage 1 (TintIndex jumps to 10). CSpellGemWnd::OnProcessFrame
//    @0x760360 steps TintIndex one a frame: up to 10 in stage 1 (darkening), down to 0 in stage 2 (fading back in,
//    then the stage returns to 0).
//  - Draw, while no spell is being cast: the time left is the larger of the gem's SpellGemETA - now and the linked
//    timer's seconds * 1000; none stops the cool-down; otherwise the cool-down (re)starts when it is not running or
//    differs from that by 1000 ms or more, with the total the spell's recast time (less its focus reuse
//    modifier) when the gem's own timer is the longer, else the time left. The cool-down is drawn every frame as a
//    rectangle of user colour 351 (eqclient.ini [TextColors] User_96) at alpha 0x40, from the gem's top moved down
//    by ftol(min((total - left) / total, 0.87) * height) to its bottom; it ends when its time is up.

#include <array>
#include <cstdint>
#include <optional>

#include "game/mechanics/fpu.h"

namespace hailstone::game {

struct SpellGemTints {
    std::array<std::uint32_t, 11> colors{}; // ARGB
    std::array<std::uint32_t, 11> alphas{};
};
SpellGemTints spell_gem_tints(std::uint32_t color);

// FUN_004ad890 (ecx: the spell): a spell's colour (ARGB) by its target type (+0x26e), with the AE duration (+0x24)
// under 3000 ms switching two of them. SetSpellGemTint takes it for a gem's background, and MakeSpellSelectMenu
// for each spell's row in the gem's memorize menu (AddMenuItem's colour).
std::uint32_t spell_target_color(std::uint8_t target_type, std::uint32_t ae_duration);

// What RefreshSpellGemButtons decides for one gem.
struct SpellGemReadiness {
    bool casting = false;       // IsValidSpellID(CastingData.SpellID)
    bool has_spell = false;     // the memorized spell is valid
    bool gem_timer_done = true; // !(now < SpellGemETA[gem])
    bool global_done = true;    // !(now < SpellCooldownETA)
    bool linked_ready = true;   // PcZoneClient_IsLinkedSpellTimerReady
    bool ready() const { return !casting && has_spell && gem_timer_done && global_done && linked_ready; }
};

struct SpellGemDisplay {
    std::int32_t stage = 0;      // spellstate +0x2d0
    std::int32_t tint_index = 0; // TintIndex +0x2d4
    std::uint32_t cooldown_begin = 0, cooldown_duration = 0; // CButtonWnd +0x21c / +0x220

    void set_stage(std::int32_t s);       // SetGemTintStage
    void refresh(bool ready);             // RefreshSpellGemButtons' choice (the icon is always set here)
    void process_frame();                 // OnProcessFrame

    // The cool-down (CButtonWnd virtuals +0x180 / +0x174 / +0x190 / +0x194).
    void set_cooldown(std::uint32_t begin, std::uint32_t duration);
    void start_cooldown(std::uint32_t now, std::uint32_t left, std::uint32_t total);
    std::uint32_t cooldown_left(std::uint32_t now) const;
    void stop_cooldown() { cooldown_begin = cooldown_duration = 0; }

    // Draw's timer part: `gem_left` = SpellGemETA - now (0 when reached), `linked_left` = the linked timer's
    // seconds left * 1000, `recast_total` = the spell's recast time less its focus reuse modifier.
    void update_cooldown(std::uint32_t now, std::uint32_t gem_left, std::uint32_t linked_left, std::uint32_t recast_total);

    // The cool-down rectangle's fraction (@0x8970a0): min((total - left) / total, 0.87) as the x87 holds it, or none
    // (and the cool-down ends when its time is up).
    std::optional<double> overlay_fraction(std::uint32_t now,
                                           mechanics::FpuPrecision p = mechanics::FpuPrecision::Single24);
};

// CButtonWnd's own cool-down (any button: the hot buttons, the Actions and combat ability windows' buttons; the
// gem above keeps the same two fields): +0x21c CoolDownBeginTime and +0x220 CoolDownDuration in EQ time (ms).
// DrawCooldown @0x8970a0 is the same rectangle as the gem's: from the top moved down by
// ftol(min(part gone, 0.87) * height) to the bottom, user colour 0x15f at alpha 0x40.
struct ButtonCoolDown {
    std::uint32_t begin = 0, duration = 0;

    // vf96 @0x897960: a zero begin or duration stops it.
    void set(std::uint32_t b, std::uint32_t d);
    // vf97 @0x897990 SetCoolDown(begin, duration) (vtable +0x184): once its time is up it stops (when it ran) and
    // returns false; otherwise it is (re)set unless it already holds these two, and returns true.
    bool set_running(std::uint32_t now, std::uint32_t b, std::uint32_t d);
    // vf95 @0x897910: ending at `end` (EQ time) after `d`.
    void set_ending(std::uint32_t end, std::uint32_t d) { set(end - d, d); }
    void stop() { begin = duration = 0; } // vf101 @0x897a30
    std::uint32_t left(std::uint32_t now) const; // vf100 @0x897a00
    bool running(std::uint32_t now) const { return now - begin < duration; }
    // DrawCooldown's fraction, none while idle (and the cool-down stops once its time is up).
    std::optional<double> overlay_fraction(std::uint32_t now,
                                           mechanics::FpuPrecision p = mechanics::FpuPrecision::Single24);
};

// FastTimeToEQTime @0x8098b0: a Util::FastTime second as EQ time (ms): eq_now + (t - fast_now) * 1000.
inline std::uint32_t fast_time_to_eq_time(std::uint32_t t, std::uint32_t fast_now, std::uint32_t eq_now) {
    return eq_now + (t - fast_now) * 1000u;
}

// The rectangle's top: top + ftol(fraction * (bottom - top)).
std::int32_t spell_gem_overlay_top(double fraction, std::int32_t top, std::int32_t bottom,
                                   mechanics::FpuPrecision p = mechanics::FpuPrecision::Single24);

// GetUserColor @0x761c00 for user colours above 255: the table's {b, g, r} as ARGB with alpha 0xff; the cool-down
// keeps alpha 0x40 (& 0x40ffffff).
constexpr std::uint32_t kSpellGemOverlayUserColor = 0x15f;
inline std::uint32_t spell_gem_overlay_color(std::uint32_t rgb) { return (0xff000000u | (rgb & 0xffffffu)) & 0x40ffffffu; }

} // namespace hailstone::game
