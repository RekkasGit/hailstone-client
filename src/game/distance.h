#pragma once

// The original's spawn distance, as used for spell range and interaction checks.
//
// Source: rof2decomp batch 48, re/src/game/display/display.cpp:
//   CDisplay::TrueDistance @0x48c2c0 — sqrt(dx² + dy² + (dz / z_scale)²), or the
//     2D distance when z_scale is 0. The sum is formed on the x87 stack, stored
//     as a float, then square-rooted.
//   CDisplay::PlayerDistance @0x48c450 — TrueDistance between two spawns'
//     positions (+0x64..+0x6c), or -1e27 (the constant at 0x9c5760) when
//     either spawn is missing. Spell range checks call it with z_scale 1.0.
//
// The x87 extended-precision sum is reproduced with double, then rounded to
// float as the original stores it before the square root; for the coordinate
// ranges EQ uses the two agree except in rare double-rounding cases.

#include <cmath>

#include <glm/glm.hpp>

namespace nmsc::game {

inline constexpr float kMissingDistance = -1e27f;

inline float true_distance(const glm::vec3& a, const glm::vec3& b, float z_scale) {
    double dx = static_cast<double>(a.x) - b.x;
    double dy = static_cast<double>(a.y) - b.y;
    double sum = dx * dx + dy * dy;
    if (z_scale != 0.0f) {
        double dz = (static_cast<double>(a.z) - b.z) / z_scale;
        sum += dz * dz;
    }
    return std::sqrt(static_cast<float>(sum));
}

// a and b are spawn positions (server reference points); nullptr = missing spawn.
inline float player_distance(const glm::vec3* a, const glm::vec3* b, float z_scale = 1.0f) {
    if (!a || !b) return kMissingDistance;
    return true_distance(*a, *b, z_scale);
}

} // namespace nmsc::game
