#include "game/spell_gem.h"

#include <cstdlib>

namespace hailstone::game {

using mechanics::Fpu;

SpellGemTints spell_gem_tints(std::uint32_t color) {
    SpellGemTints t;
    std::uint32_t step = ((color >> 16 & 0xffu) / 11u) << 16 | ((color >> 8 & 0xffu) / 11u) << 8 | (color & 0xffu) / 11u;
    for (std::uint32_t i = 0; i < 11; ++i) {
        t.colors[i] = color - step * i;
        t.alphas[i] = 0xffu - 0x15u * i;
    }
    return t;
}

std::uint32_t spell_target_color(std::uint8_t target_type, std::uint32_t ae_duration) {
    switch (target_type) {
    case 6: return 0xffcede29;
    case 8: case 21: case 22: case 23: case 28: case 30: return ae_duration < 3000 ? 0xff42de63 : 0xffde7b21;
    case 2: case 4: case 24: case 25: case 26: case 29: case 31: case 48: case 49: return 0xff5242de;
    case 3: case 40: case 41: return 0xff9c31de;
    case 42: case 44: return 0xff00decc;
    case 45: return ae_duration < 3000 ? 0xff003300 : 0xffcc3300;
    case 46: return 0xffde33de;
    case 32: case 33: case 35: case 36: case 37: return 0xff000000;
    default: return target_type > 0x31 ? 0xff000000 : 0xffde2121;
    }
}

void SpellGemDisplay::set_stage(std::int32_t s) {
    if (s == stage) return;
    if (s == 2) tint_index = 10;
    stage = s;
}

void SpellGemDisplay::refresh(bool ready) {
    if (!ready) set_stage(1);
    else if (stage == 1) set_stage(2);
}

void SpellGemDisplay::process_frame() {
    if (stage == 0) return;
    if (stage == 1) {
        if (tint_index < 10) ++tint_index;
        return;
    }
    if (tint_index < 1) {
        stage = 0;
        return;
    }
    --tint_index;
}

void SpellGemDisplay::set_cooldown(std::uint32_t begin, std::uint32_t duration) {
    if (begin == 0 || duration == 0) {
        stop_cooldown();
        return;
    }
    cooldown_begin = begin;
    cooldown_duration = duration;
}

void SpellGemDisplay::start_cooldown(std::uint32_t now, std::uint32_t left, std::uint32_t total) {
    set_cooldown(now + (left - total), total);
}

std::uint32_t SpellGemDisplay::cooldown_left(std::uint32_t now) const {
    std::uint32_t elapsed = now - cooldown_begin;
    return elapsed < cooldown_duration ? cooldown_duration - elapsed : 0;
}

void SpellGemDisplay::update_cooldown(std::uint32_t now, std::uint32_t gem_left, std::uint32_t linked_left,
                                      std::uint32_t recast_total) {
    std::uint32_t left = gem_left > linked_left ? gem_left : linked_left;
    std::uint32_t current = cooldown_left(now);
    if (left == 0) {
        stop_cooldown();
        return;
    }
    auto diff = static_cast<std::int32_t>(left - current);
    if (cooldown_duration != 0 && std::abs(static_cast<std::int64_t>(diff)) < 1000) return;
    std::uint32_t total = linked_left < gem_left ? recast_total : left;
    start_cooldown(now, left, total);
}

namespace {

// DrawCooldown @0x8970a0's fraction for {begin, duration}; `stop` when its time is up.
std::optional<double> cool_down_fraction(std::uint32_t& begin, std::uint32_t& duration, std::uint32_t now,
                                         mechanics::FpuPrecision p) {
    std::uint32_t elapsed = now - begin;
    if (elapsed >= duration) {
        begin = duration = 0;
        return std::nullopt;
    }
    std::uint32_t left = duration - elapsed;
    if (duration == 0 || left == 0) return std::nullopt;
    Fpu fpu(p);
    // fild (unsigned: + 2^32 when negative) of total - left and total, fdivp, then the smaller of it and 0.87.
    double fraction = fpu.div(static_cast<double>(duration - left), static_cast<double>(duration));
    constexpr double kMax = 0.87; // 0xa1b608
    return kMax < fraction ? kMax : fraction;
}

} // namespace

std::optional<double> SpellGemDisplay::overlay_fraction(std::uint32_t now, mechanics::FpuPrecision p) {
    return cool_down_fraction(cooldown_begin, cooldown_duration, now, p);
}

void ButtonCoolDown::set(std::uint32_t b, std::uint32_t d) {
    if (b == 0 || d == 0) {
        stop();
        return;
    }
    begin = b;
    duration = d;
}

bool ButtonCoolDown::set_running(std::uint32_t now, std::uint32_t b, std::uint32_t d) {
    if (d <= now - b) {
        if (duration != 0) stop(); // vf99 (the duration) non-zero: vf101
        return false;
    }
    if (duration != d || begin != b) set(b, d);
    return true;
}

std::uint32_t ButtonCoolDown::left(std::uint32_t now) const {
    std::uint32_t elapsed = now - begin;
    return elapsed < duration ? duration - elapsed : 0;
}

std::optional<double> ButtonCoolDown::overlay_fraction(std::uint32_t now, mechanics::FpuPrecision p) {
    return cool_down_fraction(begin, duration, now, p);
}

std::int32_t spell_gem_overlay_top(double fraction, std::int32_t top, std::int32_t bottom, mechanics::FpuPrecision p) {
    Fpu fpu(p);
    return top + Fpu::ftol(fpu.mul(fraction, static_cast<double>(bottom - top)));
}

} // namespace hailstone::game
