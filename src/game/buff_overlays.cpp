#include "game/buff_overlays.h"

#include <format>

#include "game/spells.h"

namespace nmsc::game {

SpellCounters spell_counters(const SpellData& spell, std::span<const BuffSlot> effects) {
    SpellCounters out;
    for (const BuffSlot& e : effects) {
        if (e.empty() || e.spell != spell.id) continue;
        for (std::size_t i = 0; i < 12; ++i) {
            int k = -1;
            switch (spell.effect[i]) {
            case kSpaCorruptionCounter: k = 0; break;
            case kSpaDiseaseCounter: k = 1; break;
            case kSpaCurseCounter: k = 2; break;
            case kSpaPoisonCounter: k = 3; break;
            default: break;
            }
            if (k < 0) continue;
            out.base[static_cast<std::size_t>(k)] += spell.base[i];
            out.left[static_cast<std::size_t>(k)] += e.slot_data[i];
        }
    }
    return out;
}

double buff_fade(std::int64_t ms_left, std::int64_t now_ms, bool short_song) {
    if (ms_left <= 0 || ms_left / 1000 >= 19 || short_song) return 1.0;
    std::uint64_t t = static_cast<std::uint64_t>(now_ms); // the time stamp as the original's unsigned
    return static_cast<double>(100 - static_cast<int>((t % 1000) / 10)) / 100.0;
}

std::optional<BuffOverlayText> buff_time_overlay(const SpellData& spell, std::int64_t ms_left) {
    if (spell.duration_formula == 0) return std::nullopt;
    auto seconds = static_cast<std::int32_t>(ms_left / 1000);
    if (seconds < 1) return std::nullopt;
    // SecondsToHMS @0x8098d0.
    std::int32_t h = seconds / 3600, m = (seconds - h * 3600) / 60, s = seconds - seconds / 60 * 60;
    BuffOverlayText t;
    t.text = h > 0 ? std::format("{}h", h) : m > 0 ? std::format("{}m", m) : std::format("{}s", s);
    return t;
}

std::optional<BuffOverlayText> buff_counter_overlay(const SpellCounters& c) {
    const auto& b = c.base;
    if (b[0] <= 0 && b[1] <= 0 && b[2] <= 0 && b[3] <= 0) return std::nullopt;
    int type = 0;
    std::int32_t value = static_cast<std::uint32_t>(b[0] - 1) < 999998u ? c.left[0] : 999999;
    for (int k = 1; k < 4; ++k) {
        if (b[static_cast<std::size_t>(k)] > 0 && b[static_cast<std::size_t>(k)] < value) {
            value = c.left[static_cast<std::size_t>(k)];
            type = k;
        }
    }
    if (value <= 0) return std::nullopt;
    static constexpr std::uint32_t kColours[4] = {0xffff8c00, 0xffffff00, 0xffff0000, 0xff00ff00};
    return BuffOverlayText{std::format("{}", value), kColours[type]};
}

std::optional<BuffOverlayText> buff_limit_overlay(const SpellData& spell, std::span<const BuffSlot> effects) {
    if (spell.hit_count == 0) return std::nullopt;
    std::int32_t hits = 0;
    for (const BuffSlot& e : effects)
        if (e.spell == spell.id) hits = static_cast<std::int32_t>(e.num_hits);
    if (hits <= 0) return std::nullopt;
    return BuffOverlayText{std::format("{}", hits), 0xffffffff};
}

} // namespace nmsc::game
