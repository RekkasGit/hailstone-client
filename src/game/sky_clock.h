#pragma once

// The game clock the sky is drawn for. OP_TimeOfDay sets it; between packets
// it runs at one game minute per three real seconds, as CDisplay_UpdateSky
// does (elapsed ms / 3000 minutes, see docs/formats/sky.md). Hours are the
// server's 1..24 (hour 0 is read as 24); days 1..28 of the month (the
// server's calendar, twelve months of 28 days).

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hailstone::game {

class SkyClock {
public:
    // Sets the clock to hour:minute of day `day` of month `month` (1..12, 0 unknown) at real time `now_seconds`.
    void set(int hour, int minute, double now_seconds, int day = 1, int month = 0) {
        int h = ((hour - 1) % 24 + 24) % 24; // 0..23 for hours 1..24
        base_minutes_ = h * 60.0 + minute;
        base_day_ = ((day - 1) % kMonthDays + kMonthDays) % kMonthDays;
        month_ = month;
        base_time_ = now_seconds;
        valid_ = true;
    }

    bool valid() const { return valid_; }

    // The sky hour at `now_seconds`: 1.0 .. 24.99 (24 = midnight), with minutes as a fraction.
    float hour(double now_seconds) const {
        double minutes = base_minutes_ + (now_seconds - base_time_) / 3.0;
        minutes = std::fmod(minutes, 1440.0);
        if (minutes < 0.0) minutes += 1440.0;
        return static_cast<float>(1.0 + minutes / 60.0);
    }

    // The moon's phase, 0..1 through its month: not the original's (its sky has no phases), a modern look's
    // (render::Daylight). One lunar month a calendar month: new on the 1st, full on the 15th.
    float moon_phase(double now_seconds) const {
        double days = base_day_ + (base_minutes_ + (now_seconds - base_time_) / 3.0) / 1440.0;
        double p = std::fmod(days / kMonthDays, 1.0);
        return static_cast<float>(p < 0.0 ? p + 1.0 : p);
    }

    // The blood moon, 0..1: this client's own, as the phases are. The full moon of every third month (3, 6, 9, 12)
    // turns red, wholly for the day and a half about full and fading over the half day either side, so everyone on
    // a server sees it on the same nights (a game month is 33.6 real hours: one about every four real days).
    float blood_moon(double now_seconds) const {
        if (month_ <= 0 || month_ % 3 != 0) return 0.0f;
        double days = std::abs(moon_phase(now_seconds) - 0.5) * kMonthDays; // from full
        double t = std::clamp((days - 0.75) / 0.5, 0.0, 1.0);
        return static_cast<float>(1.0 - t * t * (3.0 - 2.0 * t));
    }

    // The northern lights, 0..1: this client's own too. Each night of the calendar (noon to noon) draws its own,
    // from the day and month, so everyone on a server sees the same nights: none on three nights in five, else
    // 0.3 .. 1.
    float aurora(double now_seconds) const {
        double days = base_day_ + (base_minutes_ + (now_seconds - base_time_) / 3.0) / 1440.0 + 0.5;
        auto night = static_cast<std::uint32_t>(static_cast<std::int64_t>(std::floor(days)) + month_ * kMonthDays);
        std::uint32_t h = night * 0x9e3779b9u;
        h ^= h >> 15;
        h *= 0x85ebca6bu;
        h ^= h >> 13;
        double u = static_cast<double>(h & 0xffffu) / 65536.0;
        return u < 0.6 ? 0.0f : static_cast<float>(0.3 + 0.7 * (u - 0.6) / 0.4);
    }

    static constexpr int kMonthDays = 28;

private:
    double base_minutes_ = 11.0 * 60.0; // noon until set
    int base_day_ = 14;                 // the full moon until set
    int month_ = 0;                     // 1..12 (OP_TimeOfDay), 0 unknown
    double base_time_ = 0.0;
    bool valid_ = false;
};

} // namespace hailstone::game
