#include "game/mouse_look.h"

#include <algorithm>

namespace nmsc::game {

void MouseLook::right_down(const MouseLookOptions& o, std::uint32_t now) {
    right_down_at = now;
    // A left-button look (ItemPlacement not active): the right button joins it.
    if (active && !by_right) {
        both_buttons = true;
        return;
    }
    active = o.mouse_active;
    if (active) by_right = true; // set before the GameState 5 test (the cursor is hidden only in game)
}

MouseLook::RightUp MouseLook::right_up(std::uint32_t now) {
    RightUp out;
    both_buttons = false;
    if (active) {
        if (by_right) {
            active = false;
            if (now - right_down_at > kRightLookClickMaxHeldMs) {
                out.center_cursor = true;
                return out;
            }
        }
        if (active) return out;
    }
    out.click = true;
    return out;
}

void MouseLook::left_down(const MouseLookOptions& o, bool camera6, std::uint32_t now) {
    left_down_at = now;
    if (active && by_right) {
        both_buttons = true;
        return;
    }
    if (camera6 && o.turn_zoom) active = o.mouse_active;
    if (active) by_right = false;
}

bool MouseLook::left_up(std::uint32_t now) {
    both_buttons = false;
    if (active && !by_right) active = false;
    return !active && now - left_down_at < kLeftClickMaxHeldMs;
}

PointerStep pointer_step(float x, float y, float dx, float dy, std::int32_t speed, float width, float height) {
    if (speed > 1) {
        dx *= static_cast<float>(speed);
        dy *= static_cast<float>(speed);
    }
    const float right = std::max(width - 1.0f, 0.0f), bottom = std::max(height - 1.0f, 0.0f);
    const float nx = x + dx, ny = y + dy;
    PointerStep s;
    s.x = std::clamp(nx, 0.0f, right);
    s.y = std::clamp(ny, 0.0f, bottom);
    s.leave = (nx < 0.0f && x <= 0.0f) || (nx > right && x >= right) || (ny < 0.0f && y <= 0.0f) ||
              (ny > bottom && y >= bottom);
    return s;
}

} // namespace nmsc::game
