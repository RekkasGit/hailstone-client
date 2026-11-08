#pragma once

// The game clock (docs/mechanics.md "Frame rate"). The original runs its game rules once per drawn frame, and some of
// them (friction, the turn ramp, fades of a fixed step) are written per frame, so they depend on the frame rate.
// Hailstone (a deliberate difference, switchable: game.fixed_step) runs those rules as the original would at a steady
// 60 frames a second, whatever it draws at: a drawn frame runs the steps that came due since the last one (none, one
// or several), and the drawing goes between the last two.
//
// A step's time is what the original's millisecond clock (timeGetTime) read at 60 frames a second: the steps fall at
// floor(k x 1000 / 60) ms, so they are 16, 17 and 17 ms apart. After a long frame (a hitch, a zone's load) at most
// kMaxSteps run and the rest of the time is dropped: the rules slow down for that frame rather than running a burst
// of steps that would make the next frame long too.

#include <cstddef>
#include <cstdint>
#include <span>

namespace hailstone::core {

class StepClock {
public:
    static constexpr std::uint32_t kRate = 60;      // steps a second
    static constexpr std::uint32_t kMaxSteps = 15;  // per drawn frame: 250 ms

    struct Step {
        std::uint64_t ms = 0;         // the step's time (the caller's millisecond clock)
        std::uint32_t elapsed_ms = 0; // since the step before (16 or 17; 0 in per-frame mode, see below)
    };

    // fixed: false runs one step per drawn frame at its own time (the original's rule as written; elapsed_ms 0
    // tells the caller to measure the frame itself).
    void set_fixed(bool fixed) {
        if (fixed != fixed_) started_ = false;
        fixed_ = fixed;
    }
    bool fixed() const { return fixed_; }

    // The steps due by `now_us` (microseconds of a steady clock). The first call starts the clock with one step.
    std::span<const Step> advance(std::uint64_t now_us) {
        count_ = 0;
        now_us_ = now_us;
        const std::uint64_t now_ms = now_us / 1000;
        if (!fixed_) {
            steps_[count_++] = Step{now_ms, 0};
            last_ms_ = now_ms;
            next_ms_ = now_ms;
            return {steps_, count_};
        }
        if (!started_) {
            started_ = true;
            origin_ms_ = now_ms;
            index_ = 0;
            emit(); // the first step, at the start
            next_ms_ = at(index_);
            return {steps_, count_};
        }
        // A long frame: the steps beyond kMaxSteps are dropped and the clock starts again from now, so the steps
        // still fall 16, 17, 17 ms apart.
        std::uint64_t due = 0;
        for (std::uint64_t i = index_; at(i) <= now_ms && due <= kMaxSteps; ++i) ++due;
        if (due > kMaxSteps) {
            const std::uint64_t first = now_ms - span_ms(kMaxSteps - 1);
            origin_ms_ = first;
            index_ = 0;
            dropped_ = true;
        }
        while (count_ < kMaxSteps && at(index_) <= now_ms) emit();
        next_ms_ = at(index_);
        return {steps_, count_};
    }

    // Where this frame is between the last step and the next (0..1): what the drawing mixes the last two steps by.
    // 1 in per-frame mode (the last step is now).
    float alpha() const {
        if (!fixed_ || next_ms_ <= last_ms_) return 1.0f;
        const double from = static_cast<double>(last_ms_) * 1000.0;
        const double span = static_cast<double>(next_ms_ - last_ms_) * 1000.0;
        double a = (static_cast<double>(now_us_) - from) / span;
        if (a < 0.0) a = 0.0;
        if (a > 1.0) a = 1.0;
        return static_cast<float>(a);
    }

    // The last advance dropped time (a hitch longer than kMaxSteps): cleared by the next call to take_dropped.
    bool take_dropped() {
        bool d = dropped_;
        dropped_ = false;
        return d;
    }

private:
    // The k-th step's time from the origin, and the time `n` steps take.
    std::uint64_t at(std::uint64_t k) const { return origin_ms_ + k * 1000 / kRate; }
    static std::uint64_t span_ms(std::uint64_t n) { return n * 1000 / kRate; }
    void emit() {
        const std::uint64_t ms = at(index_);
        const std::uint32_t elapsed =
            index_ == 0 ? static_cast<std::uint32_t>(at(1) - at(0)) : static_cast<std::uint32_t>(ms - at(index_ - 1));
        steps_[count_++] = Step{ms, elapsed};
        last_ms_ = ms;
        ++index_;
    }

    bool fixed_ = true;
    bool started_ = false;
    bool dropped_ = false;
    std::uint64_t origin_ms_ = 0;
    std::uint64_t index_ = 0; // the next step's
    std::uint64_t last_ms_ = 0;
    std::uint64_t next_ms_ = 0;
    std::uint64_t now_us_ = 0;
    Step steps_[kMaxSteps];
    std::size_t count_ = 0;
};

} // namespace hailstone::core
