#include "platform/mouse_capture.h"

namespace hailstone::platform {

void MouseCapture::set(bool captured, Cursor& cursor) {
    if (captured == captured_) return;
    captured_ = captured;
    if (captured) {
        cursor.position(x_, y_);
        // A warp to where the cursor already is (no move): the compositor's place to put it back when the look ends
        // (KWin), made before the pointer is locked.
        if (cursor.focused()) cursor.warp(x_, y_);
        cursor.set_relative(true);
        return;
    }
    // Unfocused, SDL has left relative mode already: the cursor is the rest of the desktop's, not to be moved.
    if (cursor.focused()) cursor.warp(x_, y_); // still relative: SDL's place only
    cursor.set_relative(false);                // SDL puts the system's cursor there
}

void MouseCapture::set_held(bool captured, float x, float y) {
    if (captured == captured_) return;
    captured_ = captured;
    if (captured) hold_at(x, y);
}

} // namespace hailstone::platform
