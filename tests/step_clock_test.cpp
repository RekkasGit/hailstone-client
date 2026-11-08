// core/step_clock.h: the 60 Hz game clock steps as the original's millisecond clock read at 60 frames a second,
// whatever the drawn frame rate.
#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "core/step_clock.h"

using hailstone::core::StepClock;

namespace {

// Drawn frames `frame_us` apart for `total_us`: every step's time and elapsed, in order.
std::vector<StepClock::Step> run(std::uint64_t frame_us, std::uint64_t total_us) {
    StepClock clock;
    std::vector<StepClock::Step> out;
    for (std::uint64_t t = 1'000'000; t <= 1'000'000 + total_us; t += frame_us)
        for (const auto& s : clock.advance(t)) out.push_back(s);
    return out;
}

} // namespace

TEST_CASE("Step clock: 60 steps a second, 16, 17 and 17 ms apart, at any drawn frame rate") {
    // 1000, 240, 144, 60, 30, 15 and 7 frames a second over 3 s.
    for (std::uint64_t frame_us : {1000u, 4167u, 6944u, 16667u, 33333u, 66667u, 142857u}) {
        CAPTURE(frame_us);
        auto steps = run(frame_us, 3'000'000);
        // The first step at the start, then one each 1000 / 60 ms up to the last drawn frame: every step due by it.
        const std::uint64_t last_ms = (3'000'000 / frame_us) * frame_us / 1000;
        std::size_t due = 0;
        while (due * 1000 / 60 <= last_ms) ++due;
        CHECK(steps.size() == due);
        CHECK(steps.front().ms == 1000);
        for (std::size_t i = 1; i < steps.size(); ++i) {
            CHECK(steps[i].ms - steps[i - 1].ms == steps[i].elapsed_ms);
            CHECK((steps[i].elapsed_ms == 16 || steps[i].elapsed_ms == 17));
            CHECK(steps[i].ms == 1000 + i * 1000 / 60);
        }
    }
}

TEST_CASE("Step clock: a frame between steps runs none, and alpha says where it is") {
    StepClock clock;
    CHECK(clock.advance(0).size() == 1); // started: step 0 at 0 ms, the next at 16
    CHECK(clock.advance(4000).empty());
    CHECK(clock.alpha() == doctest::Approx(0.25));
    CHECK(clock.advance(12000).empty());
    CHECK(clock.alpha() == doctest::Approx(0.75));
    auto s = clock.advance(16000);
    REQUIRE(s.size() == 1);
    CHECK(s[0].ms == 16);
    CHECK(clock.alpha() == doctest::Approx(0.0));
}

TEST_CASE("Step clock: a hitch runs at most kMaxSteps and drops the rest") {
    StepClock clock;
    clock.advance(0);
    CHECK(!clock.take_dropped());
    auto s = clock.advance(2'000'000); // a 2 s frame
    REQUIRE(s.size() == StepClock::kMaxSteps);
    CHECK(s.back().ms == 2000);
    for (std::size_t i = 1; i < s.size(); ++i) CHECK((s[i].ms - s[i - 1].ms == 16 || s[i].ms - s[i - 1].ms == 17));
    CHECK(clock.take_dropped());
    CHECK(!clock.take_dropped());
    // On from there, 60 a second again.
    auto next = clock.advance(2'000'000 + 100'000);
    CHECK(next.size() == 6);
    // A frame of exactly kMaxSteps steps drops nothing.
    StepClock c2;
    c2.advance(0);
    CHECK(c2.advance(250'000).size() == StepClock::kMaxSteps);
    CHECK(!c2.take_dropped());
}

TEST_CASE("Step clock: per-frame mode steps once a drawn frame, at its own time") {
    StepClock clock;
    clock.set_fixed(false);
    for (std::uint64_t t : {1000u, 1500u, 90'000u, 90'200u}) {
        auto s = clock.advance(t);
        REQUIRE(s.size() == 1);
        CHECK(s[0].ms == t / 1000);
        CHECK(s[0].elapsed_ms == 0);
        CHECK(clock.alpha() == 1.0f);
    }
    // Back to fixed: it starts again with one step.
    clock.set_fixed(true);
    CHECK(clock.advance(100'000).size() == 1);
    CHECK(clock.advance(110'000).empty());
}
