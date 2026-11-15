#include <doctest/doctest.h>

#include "app/perf_ui.h"
#include "core/frame_sections.h"

using namespace hailstone::app;

TEST_CASE("perf: a hitch is 2.5 times the typical frame, 20 ms at least") {
    CHECK_FALSE(is_hitch(60.0f, 0.0f)); // no typical time yet
    CHECK_FALSE(is_hitch(19.0f, 4.0f));
    CHECK(is_hitch(20.0f, 4.0f));
    CHECK_FALSE(is_hitch(40.0f, 16.7f));
    CHECK(is_hitch(42.0f, 16.7f));
}

TEST_CASE("perf: a hitch's log line lists its phases longest first") {
    PhaseTimes phases{};
    phases[static_cast<std::size_t>(FramePhase::Update)] = 50.0f;
    phases[static_cast<std::size_t>(FramePhase::Record)] = 3.0f;
    phases[static_cast<std::size_t>(FramePhase::Ui)] = 0.5f; // under 1 ms: left out
    CHECK(hitch_text(60.0f, 8.0f, phases) == "hitch 60.0 ms (typical 8.0): Update 50.0 other 6.5 Record 3.0");
}

TEST_CASE("perf: the sections of a hitch follow its phases") {
    PhaseTimes phases{};
    phases[static_cast<std::size_t>(FramePhase::Ui)] = 50.0f;
    CHECK(hitch_text(52.0f, 8.0f, phases, {{"draw_ui", 2.0f}, {"Lua UI frame", 47.5f}, {"perf window", 0.4f}}) ==
          "hitch 52.0 ms (typical 8.0): UI 50.0 other 2.0; in it: Lua UI frame 47.5, draw_ui 2.0");
}

TEST_CASE("perf: frame sections add up by name and are emptied when taken") {
    namespace fs = hailstone::core::frame_sections;
    fs::take();
    fs::add("a", 1.0f);
    fs::add("b", 2.0f);
    fs::add("a", 3.0f);
    auto taken = fs::take();
    REQUIRE(taken.size() == 2);
    CHECK(taken[0].second == doctest::Approx(4.0f));
    CHECK(taken[1].second == doctest::Approx(2.0f));
    CHECK(fs::take().empty());
}

TEST_CASE("perf: frame generation's rates count every image displayed against the real frames") {
    // Four real frames of 20 ms: three had an in-between frame.
    const FrameRates r = frame_rates({20.0f, 20.0f, 20.0f, 20.0f}, {2, 2, 1, 2});
    CHECK(r.rendered_fps == doctest::Approx(50.0f));
    CHECK(r.displayed_fps == doctest::Approx(87.5f));
    CHECK(r.generated_share == doctest::Approx(0.75f));
    // Without frame generation both rates are the same.
    const FrameRates off = frame_rates({10.0f, 10.0f}, {1, 1});
    CHECK(off.displayed_fps == doctest::Approx(off.rendered_fps));
    CHECK(off.generated_share == doctest::Approx(0.0f));
    // A cut's frame shows the real one twice: two images, but no in-between frame.
    const FrameRates cut = frame_rates({20.0f, 20.0f}, {2, 2}, {1, 0});
    CHECK(cut.displayed_fps == doctest::Approx(100.0f));
    CHECK(cut.generated_share == doctest::Approx(0.5f));
    // Nothing yet, or histories that do not match: no rates.
    CHECK(frame_rates({}, {}).rendered_fps == 0.0f);
    CHECK(frame_rates({10.0f}, {}).displayed_fps == 0.0f);
}
