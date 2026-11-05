#pragma once

// What mouse look does with the system's cursor (platform::Window::set_mouse_captured). The original's look
// (MouseLookEngage @0x518870, CEverQuest::RMouseDown @0x518910) hides the cursor and keeps its place
// (CXWndManager::StoredMousePos); the release (LMouseUp @0x5384d0, RMouseUp @0x537d30, __ProcessMouseEvents @0x539e60
// when the player dies) shows it and puts it back there with SetCursorPos. RMouseUp's CenterMouseCursor @0x5fa1c0
// after a look held over 500 ms only sets the game's own copy (__Mouse), which the next frame's MouseCursorClamp
// @0x5fa7f0 reads back from the system's cursor: the cursor comes back where it was taken after every look.
//
// This client's look is SDL's relative mode, and SDL keeps its own place for the cursor that the raw, unaccelerated
// motion moves during the look. Leaving relative mode, SDL puts the system's cursor there (on Wayland a pointer
// warp, the only time a client can place it); a warp made while still in relative mode only sets that place. So the
// cursor goes back by setting SDL's place first and then leaving relative mode: one move, to where the cursor was
// taken (on Wayland the locked pointer never left it). Warping after leaving relative mode instead moved the cursor
// twice (to the motion's sum, then back), and nothing at all should move it when the window is not focused.
//
// On KWin (6.8, the user's desktop) a warp (wp_pointer_warp_v1) also sets the window surface's pointer lock hint,
// applied at its next commit and kept, and the end of a lock first puts the pointer at that hint: the last warp
// before the look, which was the previous look's put back. Every click then took the cursor to the previous click's
// place and back (reproduced in a nested KWin driven through libei). So taking the cursor first warps it to where
// it is, which moves nothing and makes the hint this look's place; the frame drawn before the release commits it.

namespace nmsc::platform {

class MouseCapture {
public:
    // The system's cursor as SDL handles it (platform::Window's; a model of it in the tests).
    struct Cursor {
        virtual ~Cursor() = default;
        virtual void position(float& x, float& y) = 0; // SDL_GetMouseState: SDL's place for it
        virtual void set_relative(bool on) = 0;        // SDL_SetWindowRelativeMouseMode
        virtual void warp(float x, float y) = 0;       // SDL_WarpMouseInWindow
        virtual bool focused() = 0;                    // the window has the keyboard focus (relative mode applies)
    };

    bool captured() const { return captured_; }
    float x() const { return x_; }
    float y() const { return y_; }

    // Takes the cursor (where it is now is where it comes back) or gives it back.
    void set(bool captured, Cursor& cursor);
    // Mouse look with the window's own cursor (Window::set_pointer_speed) holding the system's already: only the
    // place is kept, nothing is asked of SDL.
    void set_held(bool captured, float x, float y);
    // Where the cursor comes back (Window::center_mouse).
    void hold_at(float x, float y) {
        x_ = x;
        y_ = y;
    }

private:
    bool captured_ = false;
    float x_ = 0.0f, y_ = 0.0f;
};

} // namespace nmsc::platform
