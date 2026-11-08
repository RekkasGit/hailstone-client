#pragma once

// Hailstone's overlap fade (docs/graphics.md "Name plates"). Names stay at their projected heads; only alpha
// changes. This CPU-only helper is shared by the client and headless tests.

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace hailstone::game {

struct PlateBox {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    float depth = 0.0f; // distance along the view direction; smaller is nearer
    bool front = false; // target: drawn last and never faded
};

inline constexpr float kPlateFadeFrom = 0.10f;
inline constexpr float kPlateFadeTo = 0.20f;
inline constexpr float kPlateCoveredAlpha = 0.10f;

// Far to near, targets last; ties preserve the input order.
std::vector<std::size_t> plate_draw_order(const std::vector<PlateBox>& boxes);

// Largest fraction of a name's box covered by any single nearer name. Under 10% coverage it is fully visible;
// from 20% on it has 10% alpha, with smoothstep in between. A target stays fully visible at any depth.
std::vector<float> plate_target_alpha(const std::vector<PlateBox>& boxes);

// Whether the scene hides this name where it is nearer (docs/graphics.md "Name plates"): every name, the target's too,
// unless it is the target and the option target_plate_through_walls (Hailstone's, off by default) is on. The
// original's name sprites give no sight of a target behind a wall.
constexpr bool plate_hidden_by_scene(bool target, bool target_through_walls) { return !(target && target_through_walls); }

class PlateFade {
public:
    // New names start at their goal. Existing ones follow with a 0.3-second time constant; a newly selected target
    // immediately becomes fully visible, also updating its stored alpha. No position or distance state is kept.
    float step(std::uint32_t id, float alpha_target, float dt, bool front = false);
    // Call once after stepping every name; a name absent for a frame is forgotten.
    void end_frame();
    void clear() { entries_.clear(); }

private:
    struct Entry {
        float alpha = 1.0f;
        bool seen = false;
    };
    std::unordered_map<std::uint32_t, Entry> entries_;
};

} // namespace hailstone::game
