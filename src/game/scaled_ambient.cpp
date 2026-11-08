#include "game/scaled_ambient.h"

#include <cmath>

#include "game/collision.h"
#include "game/mechanics/fpu.h"

namespace hailstone::game {

namespace {

constexpr float kFloor = 0.1f;    // 0x1013377c
constexpr float kPerMs = 0.004f;  // 0x10136808
constexpr float kRadius = 0.5f;   // 0x10131c1c

struct Probe {
    glm::vec3 offset; // from the actor's position
    float down;       // the sweep's length, straight down
};

constexpr Probe kProbes[8] = {
    {{0.0f, 0.0f, 0.0f}, 20.0f},     {{0.0f, 0.0f, 20.0f}, 20.0f},    {{0.0f, 0.0f, 0.0f}, 400.0f},
    {{0.0f, 0.0f, 100.0f}, 100.0f},  {{10.0f, 0.0f, 100.0f}, 500.0f}, {{-10.0f, 0.0f, 100.0f}, 500.0f},
    {{0.0f, 10.0f, 100.0f}, 500.0f}, {{0.0f, -10.0f, 100.0f}, 500.0f},
};

} // namespace

float ScaledAmbient::probe(const glm::vec3& position, const CollisionMesh& mesh) {
    for (const Probe& p : kProbes) {
        glm::vec3 from = position + p.offset;
        SphereSweep s = SphereSweep::make(from, from - glm::vec3(0.0f, 0.0f, p.down), kRadius);
        if (!s.valid()) continue;
        float light = mesh.triangle_light(mesh.sweep_sphere_hit(s));
        if (std::isnan(light)) continue; // no hit, or a placed object
        return kFloor > light ? kFloor : light;
    }
    return kFloor;
}

float ScaledAmbient::update(std::uint32_t now_ms, const glm::vec3& position, const CollisionMesh* mesh) {
    if (position != last_position_) {
        dirty_ = true;
        last_position_ = position;
    }
    if (dirty_ && now_ms - last_probe_ > 1000u) {
        target_ = mesh ? probe(position, *mesh) : kFloor;
        dirty_ = false;
        last_probe_ = now_ms;
    }
    if (level_ != target_) {
        const mechanics::Fpu fpu;
        double t = fpu.mul(static_cast<double>(now_ms - last_update_), static_cast<double>(kPerMs));
        last_update_ = now_ms;
        if (1.0 > t)
            level_ = static_cast<float>(fpu.add(fpu.mul(fpu.sub(target_, level_), t), level_));
        else
            level_ = target_;
    } else {
        last_update_ = now_ms;
    }
    return level_;
}

} // namespace hailstone::game
