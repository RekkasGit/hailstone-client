// The main loop's frame cap and drawn frames (app/frame_pace.h): display.max_fps, and display.background_fps while
// another window has the focus. The loop itself (app/client_app.cpp) is not driven here.

#include <doctest/doctest.h>

#include <chrono>
#include <limits>

#include "app/frame_pace.h"

using namespace hailstone;
using std::chrono::milliseconds;

TEST_CASE("Frame pace: focused, or a background rate of 0, keeps the frame limit and draws every frame") {
    for (int bg : {0, app::kBackgroundNoDraw, 1, 5, 30}) {
        auto p = app::frame_pace(144, bg, true, milliseconds(0));
        CHECK(p.cap == 144);
        CHECK(p.draw);
    }
    auto p = app::frame_pace(0, 0, false, milliseconds(0));
    CHECK(p.cap == 0);
    CHECK(p.draw);
}

TEST_CASE("Frame pace: a background rate of 10 or more caps the loop and draws every frame") {
    CHECK(app::frame_pace(0, 30, false, milliseconds(0)).cap == 30);
    CHECK(app::frame_pace(144, 60, false, milliseconds(0)).cap == 60);
    CHECK(app::frame_pace(30, 60, false, milliseconds(0)).cap == 30); // the lower of the two
    CHECK(app::frame_pace(0, 10, false, milliseconds(0)).draw);
}

TEST_CASE("Frame pace: below 10 in the background the loop runs at 10 and draws that many frames a second") {
    for (int bg : {1, 5, app::kBackgroundNoDraw}) CHECK(app::frame_pace(0, bg, false, milliseconds(0)).cap == 10);
    // 1: once a second, on the loop's tenth tick (not the eleventh after a late wake).
    CHECK_FALSE(app::frame_pace(0, 1, false, milliseconds(900)).draw);
    CHECK(app::frame_pace(0, 1, false, milliseconds(1000)).draw);
    CHECK(app::frame_pace(0, 1, false, milliseconds(990)).draw);
    // 5: every second tick.
    CHECK_FALSE(app::frame_pace(0, 5, false, milliseconds(100)).draw);
    CHECK(app::frame_pace(0, 5, false, milliseconds(200)).draw);
    // 0 (kBackgroundNoDraw): never.
    CHECK_FALSE(app::frame_pace(0, app::kBackgroundNoDraw, false, milliseconds(60000)).draw);
}

TEST_CASE("Frame pace: display.background_fps as it is kept") {
    CHECK(app::background_fps_setting(0) == 0);
    CHECK(app::background_fps_setting(-1) == app::kBackgroundNoDraw);
    CHECK(app::background_fps_setting(-50) == app::kBackgroundNoDraw);
    CHECK(app::background_fps_setting(1) == 1);
    CHECK(app::background_fps_setting(5) == 5);
    CHECK(app::background_fps_setting(5000) == 1000);
}

TEST_CASE("Frame pace: the auto cap sits just under the display's refresh rate") {
    CHECK(app::auto_frame_cap(60.0f) == 59);
    CHECK(app::auto_frame_cap(59.94f) == 58);
    CHECK(app::auto_frame_cap(120.0f) == 116);
    CHECK(app::auto_frame_cap(144.0f) == 138);
    CHECK(app::auto_frame_cap(165.0f) == 157);
    CHECK(app::auto_frame_cap(240.0f) == 224);
    CHECK(app::auto_frame_cap(360.0f) == 324);
    // A refresh the system does not give (0) or nonsense: no cap.
    CHECK(app::auto_frame_cap(0.0f) == 0);
    CHECK(app::auto_frame_cap(-60.0f) == 0);
    CHECK(app::auto_frame_cap(5000.0f) == 0);
    CHECK(app::auto_frame_cap(std::numeric_limits<float>::quiet_NaN()) == 0);

    // display.max_fps as kept, and the cap a frame takes from it.
    CHECK(app::max_fps_setting(app::kMaxFpsAuto) == app::kMaxFpsAuto);
    CHECK(app::max_fps_setting(0) == 0);
    CHECK(app::max_fps_setting(-7) == 0);
    CHECK(app::max_fps_setting(5) == 10);
    CHECK(app::max_fps_setting(144) == 144);
    CHECK(app::max_fps_setting(5000) == 1000);
    CHECK(app::effective_max_fps(app::kMaxFpsAuto, 144.0f) == 138);
    CHECK(app::effective_max_fps(app::kMaxFpsAuto, 0.0f) == 0);
    CHECK(app::effective_max_fps(144, 60.0f) == 144); // a number is taken as it is, whatever the display
    CHECK(app::effective_max_fps(0, 144.0f) == 0);
}

TEST_CASE("Frame pace: the cap's schedule runs from the last deadline, and from now after a long frame") {
    using std::chrono::microseconds;
    using T = std::chrono::steady_clock::time_point;
    const T start{};
    const microseconds period(6944); // 144 a second
    // On time: one period after the last deadline, not after the moment the frame began its work.
    CHECK(app::next_frame_deadline(start, start + microseconds(100), period) == start + period);
    // A frame that ran past its deadline starts at once, and the schedule goes on from then: no catch-up burst.
    const T late = start + period + microseconds(10);
    CHECK(app::next_frame_deadline(start, late, period) == late);
    const T hitch = start + 5 * period;
    CHECK(app::next_frame_deadline(start, hitch, period) == hitch);
    CHECK(app::next_frame_deadline(hitch, hitch + microseconds(500), period) == hitch + period);
    // A wait that ends a little late is not carried into the next frame: the deadlines stay on the period's grid
    // as long as each frame's work fits in its period.
    T deadline = start;
    for (int i = 1; i <= 1000; ++i) {
        deadline = app::next_frame_deadline(deadline, deadline + microseconds(300), period);
        REQUIRE(deadline == start + i * period);
    }
}
