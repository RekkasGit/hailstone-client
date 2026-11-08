#include "game/cloud_patches.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace hailstone::game {

float cloud_random(int rand_value, float min, float max) {
    return static_cast<float>(rand_value) / 32767.0f * (max - min) + min;
}

void advance_cloud_patch(CloudPatch& p, float dt_ms) {
    p.angle += p.velocity * dt_ms;
    p.rotation = std::fmod(p.rotation + p.spin * dt_ms, 6.2831854f);
    p.age_ms += dt_ms;
}

CloudLayer::CloudLayer(assets::CloudDefinition def, std::function<int()> rand)
    : def_(std::move(def)), rand_(std::move(rand)) {}

CloudPatch CloudLayer::spawn(float angle) {
    CloudPatch p;
    p.angle = angle;
    p.velocity = cloud_random(rand_(), def_.velocity_min, def_.velocity_max);
    p.spin = cloud_random(rand_(), def_.spin_min, def_.spin_max);
    p.rotation = cloud_random(rand_(), def_.rotation_min, def_.rotation_max);
    p.offset = cloud_random(rand_(), def_.offset_min, def_.offset_max);
    p.size_x = cloud_random(rand_(), def_.size_min, def_.size_max);
    p.size_y = cloud_random(rand_(), def_.size_min, def_.size_max);
    float duration = cloud_random(rand_(), def_.duration_min, def_.duration_max);
    p.hold_ms = duration * 1000.0f;
    return p;
}

void CloudLayer::fill() {
    const int n = std::max(def_.desired_count, 0);
    const float va = def_.visible_angle;
    patches_.clear();
    for (int i = 0; i < n; ++i)
        patches_.push_back(spawn(static_cast<float>(i) * (2.0f * va / static_cast<float>(n)) +
                                 (std::numbers::pi_v<float> / 2.0f - va)));
}

void CloudLayer::update(float dt_ms) {
    for (auto& p : patches_) advance_cloud_patch(p, dt_ms);
    // A patch with a Duration fades out after its hold and is dropped.
    std::erase_if(patches_, [](const CloudPatch& p) {
        return p.hold_ms > 0.0f && p.age_ms > p.fade_in_ms + p.hold_ms + 5000.0f;
    });
    // CCloudDefinition_Update @0x10028140 tops the count up to DesiredCount.
    // A later spawn starts at the caller's angle, which is not traced: the
    // start of the visible arc here, after a SpawnInterval, fading in 5000 ms.
    if (static_cast<int>(patches_.size()) >= def_.desired_count) return;
    spawn_timer_ms_ -= dt_ms;
    if (spawn_timer_ms_ > 0.0f) return;
    CloudPatch p = spawn(std::numbers::pi_v<float> / 2.0f - def_.visible_angle);
    p.fade_in_ms = 5000.0f;
    patches_.push_back(p);
    spawn_timer_ms_ = cloud_random(rand_(), def_.spawn_interval_min, def_.spawn_interval_max) * 1000.0f;
}

float CloudLayer::opacity(const CloudPatch& p) {
    if (p.fade_in_ms > 0.0f && p.age_ms < p.fade_in_ms) return std::clamp(p.age_ms / p.fade_in_ms, 0.0f, 1.0f);
    float t = p.age_ms - p.fade_in_ms;
    if (p.hold_ms <= 0.0f || t <= p.hold_ms) return 1.0f;
    return std::clamp(1.0f - (t - p.hold_ms) / 5000.0f, 0.0f, 1.0f);
}

} // namespace hailstone::game
