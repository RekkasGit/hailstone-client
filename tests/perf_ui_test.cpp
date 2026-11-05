#include <doctest/doctest.h>

#include "app/perf_ui.h"
#include "core/frame_sections.h"

using namespace nmsc::app;

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
    namespace fs = nmsc::core::frame_sections;
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
