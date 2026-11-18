// The client's command-line flags: the game window at start (app/window_desc.h: the saved window, unless
// --window-size asked for a size).
#include <doctest/doctest.h>

#include "app/window_desc.h"

using namespace hailstone;

namespace {
Config saved_window() {
    Config c;
    c.parse("window.width=1920\nwindow.height=1080\nwindow.x=40\nwindow.y=50\nwindow.maximized=1\n");
    return c;
}
} // namespace

TEST_CASE("window_desc: without --window-size the saved window comes back") {
    const Config c = saved_window();
    app::ClientOptions o;
    platform::WindowDesc d = app::window_desc(o, &c);
    CHECK(d.width == 1920);
    CHECK(d.height == 1080);
    REQUIRE(d.position);
    CHECK(d.position->first == 40);
    CHECK(d.position->second == 50);
    CHECK(d.maximized);
}

TEST_CASE("window_desc: --window-size 1280x720 is a size asked for, not the default") {
    const Config c = saved_window();
    app::ClientOptions o;
    o.window_width = 1280;
    o.window_height = 720;
    o.window_size_given = true;
    platform::WindowDesc d = app::window_desc(o, &c);
    CHECK(d.width == 1280);
    CHECK(d.height == 720);
    CHECK_FALSE(d.position);
    CHECK_FALSE(d.maximized);
}

TEST_CASE("window_desc: another --window-size and no saved settings") {
    app::ClientOptions o;
    o.window_width = 800;
    o.window_height = 600;
    o.window_size_given = true;
    const Config c = saved_window();
    CHECK(app::window_desc(o, &c).width == 800);
    app::ClientOptions plain;
    CHECK(app::window_desc(plain, nullptr).width == 1280);
    CHECK(app::window_desc(plain, nullptr).height == 720);
}

