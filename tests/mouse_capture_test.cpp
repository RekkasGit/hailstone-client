// Mouse look's hold on the system's cursor (platform/mouse_capture.h) against a model of SDL 3's relative mode:
// SDL keeps its own place for the cursor, which relative motion moves; a warp in relative mode sets only that place;
// leaving relative mode with the focus puts the system's cursor there (SDL_SetRelativeMouseMode's warp back). And of
// KWin's pointer lock: a warp sets the surface's lock hint (at its next commit), where the end of a lock puts the
// pointer before anything else.

#include <initializer_list>

#include <doctest/doctest.h>

#include "platform/mouse_capture.h"

using hailstone::platform::MouseCapture;

namespace {

struct SdlModel final : MouseCapture::Cursor {
    bool wayland = true;           // the pointer is locked in relative mode (else the hidden cursor moves)
    bool has_focus = true;
    bool relative = false;
    float sys_x = 0.0f, sys_y = 0.0f; // the cursor the user sees
    float sdl_x = 0.0f, sdl_y = 0.0f; // SDL's place for it (SDL_GetMouseState)
    int moves = 0;                    // times the program moved the system's cursor (to another place)
    bool kwin = true;                 // the compositor keeps a lock hint
    bool has_hint = false, has_pending = false;
    float hint_x = 0.0f, hint_y = 0.0f, pending_x = 0.0f, pending_y = 0.0f;

    void place(float x, float y) {
        if (x != sys_x || y != sys_y) ++moves;
        sys_x = sdl_x = x;
        sys_y = sdl_y = y;
    }
    // A frame drawn: the surface's commit.
    void commit() {
        if (!has_pending) return;
        has_hint = true;
        hint_x = pending_x;
        hint_y = pending_y;
        has_pending = false;
    }
    // A warp request to the compositor (wp_pointer_warp_v1).
    void protocol_warp(float x, float y) {
        if (kwin) {
            has_pending = true;
            pending_x = x;
            pending_y = y;
        }
        place(x, y);
    }
    // The user's mouse: relative motion adds up in SDL's place (raw, unaccelerated: not where the cursor would be).
    void motion(float dx, float dy) {
        sdl_x += dx;
        sdl_y += dy;
        if (!relative || !wayland) {
            sys_x += dx;
            sys_y += dy;
        }
        if (!relative) {
            sdl_x = sys_x;
            sdl_y = sys_y;
        }
    }
    void position(float& x, float& y) override {
        x = sdl_x;
        y = sdl_y;
    }
    void set_relative(bool on) override {
        if (!has_focus) return; // SDL applies it when the window gets the focus
        if (on == relative) return;
        relative = on;
        if (on || !wayland) {
            if (!on) protocol_warp(sdl_x, sdl_y);
            return;
        }
        // The lock ends: KWin puts the pointer at the hint first, then SDL's warp back comes.
        const float x = sdl_x, y = sdl_y;
        if (kwin && has_hint) place(hint_x, hint_y);
        protocol_warp(x, y);
    }
    void warp(float x, float y) override {
        if (relative) {
            sdl_x = x;
            sdl_y = y;
            return;
        }
        protocol_warp(x, y);
    }
    bool focused() override { return has_focus; }
};

} // namespace

TEST_CASE("mouse capture: a click that starts a look gives the cursor back where it was, in one move") {
    for (bool wayland : {true, false}) {
        SdlModel sdl;
        sdl.wayland = wayland;
        sdl.sys_x = sdl.sdl_x = 400.0f;
        sdl.sys_y = sdl.sdl_y = 300.0f;
        MouseCapture cap;
        cap.set(true, sdl);
        CHECK(cap.captured());
        CHECK(sdl.relative);
        CHECK(sdl.moves == 0); // taking it moves nothing
        sdl.motion(37.0f, -12.0f); // the hand moved a little during the click
        sdl.motion(5.0f, 3.0f);
        cap.set(false, sdl);
        CHECK_FALSE(sdl.relative);
        CHECK(sdl.sys_x == 400.0f);
        CHECK(sdl.sys_y == 300.0f);
        CHECK(sdl.sdl_x == 400.0f);
        CHECK(sdl.sdl_y == 300.0f);
        // SDL's own move back, straight to the place, not to the motion's sum first (on Wayland the locked pointer
        // never left it: no move at all).
        CHECK(sdl.moves == (wayland ? 0 : 1));
    }
}

TEST_CASE("mouse capture: taken and given back again is a no-op") {
    SdlModel sdl;
    sdl.sys_x = sdl.sdl_x = 10.0f;
    sdl.sys_y = sdl.sdl_y = 20.0f;
    MouseCapture cap;
    cap.set(false, sdl); // not captured: nothing
    CHECK(sdl.moves == 0);
    cap.set(true, sdl);
    cap.set(true, sdl); // every frame of the look: nothing more
    CHECK(sdl.moves == 0);
    cap.set(false, sdl);
    cap.set(false, sdl);
    CHECK(sdl.moves == 0); // back where it was, which it never left
    CHECK(sdl.sys_x == 10.0f);
}

TEST_CASE("mouse capture: the focus lost during a look leaves the cursor alone") {
    SdlModel sdl;
    sdl.sys_x = sdl.sdl_x = 100.0f;
    sdl.sys_y = sdl.sdl_y = 100.0f;
    MouseCapture cap;
    cap.set(true, sdl);
    // Alt-Tab: SDL leaves relative mode itself; the cursor then belongs to another window.
    sdl.relative = false;
    sdl.has_focus = false;
    sdl.sys_x = sdl.sdl_x = 900.0f;
    cap.set(false, sdl);
    CHECK(sdl.moves == 0);
    CHECK(sdl.sys_x == 900.0f);
    CHECK_FALSE(cap.captured());
}

TEST_CASE("mouse capture: hold_at moves where it comes back (the recentre key during a look)") {
    SdlModel sdl;
    sdl.sys_x = sdl.sdl_x = 50.0f;
    sdl.sys_y = sdl.sdl_y = 60.0f;
    MouseCapture cap;
    cap.set(true, sdl);
    cap.hold_at(640.0f, 360.0f);
    cap.set(false, sdl);
    CHECK(sdl.sys_x == 640.0f);
    CHECK(sdl.sys_y == 360.0f);
    CHECK(sdl.moves == 1);
}

TEST_CASE("mouse capture: with the window's own cursor holding the system's, only the place is kept") {
    MouseCapture cap;
    cap.set_held(true, 123.0f, 45.0f);
    CHECK(cap.captured());
    CHECK(cap.x() == 123.0f);
    CHECK(cap.y() == 45.0f);
    cap.set_held(true, 1.0f, 1.0f); // already captured: the first place stays
    CHECK(cap.x() == 123.0f);
    cap.set_held(false, 0.0f, 0.0f);
    CHECK_FALSE(cap.captured());
    CHECK(cap.x() == 123.0f);
}

TEST_CASE("mouse capture: clicks in different places never take the cursor to an earlier one (KWin's lock hint)") {
    SdlModel sdl;
    MouseCapture cap;
    const float spots[][2] = {{400.0f, 272.0f}, {600.0f, 372.0f}, {800.0f, 472.0f}, {300.0f, 572.0f}};
    int frames = 0;
    for (const auto& spot : spots) {
        // The user's mouse goes there (not a warp: no hint).
        sdl.sys_x = sdl.sdl_x = spot[0];
        sdl.sys_y = sdl.sdl_y = spot[1];
        sdl.commit();
        const int before = sdl.moves;
        cap.set(true, sdl); // the press starts a look (MouseTurnZoom)
        sdl.commit();       // a frame or more during the click
        ++frames;
        if (frames == 3) sdl.motion(30.0f, 10.0f); // a held one that turns the camera a little
        cap.set(false, sdl);
        sdl.commit();
        CHECK(sdl.sys_x == spot[0]);
        CHECK(sdl.sys_y == spot[1]);
        CHECK(sdl.moves == before); // the cursor never left the place clicked
    }
}
