#include "game/nameplate_overlap.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace nmsc::game {
namespace {

bool nearer(const PlateBox& a, const PlateBox& b) {
    if (a.front != b.front) return a.front;
    return a.depth < b.depth;
}

float overlap_area(const PlateBox& a, const PlateBox& b) {
    const float w = std::min(a.x1, b.x1) - std::max(a.x0, b.x0);
    const float h = std::min(a.y1, b.y1) - std::max(a.y0, b.y0);
    return std::max(w, 0.0f) * std::max(h, 0.0f);
}

} // namespace

std::vector<std::size_t> plate_draw_order(const std::vector<PlateBox>& boxes) {
    std::vector<std::size_t> order(boxes.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return nearer(boxes[b], boxes[a]); });
    return order;
}

std::vector<float> plate_target_alpha(const std::vector<PlateBox>& boxes) {
    std::vector<float> alpha(boxes.size(), 1.0f);
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        const PlateBox& b = boxes[i];
        if (b.front) continue;
        const float own = std::max(b.x1 - b.x0, 0.0f) * std::max(b.y1 - b.y0, 0.0f);
        if (own <= 0.0f) continue;
        float covered = 0.0f;
        for (std::size_t j = 0; j < boxes.size(); ++j) {
            if (i != j && nearer(boxes[j], b)) {
                covered = std::max(covered, overlap_area(b, boxes[j]) / own);
                if (covered >= kPlateFadeTo) break; // more overlap cannot fade this name any further
            }
        }
        const float t = std::clamp((covered - kPlateFadeFrom) / (kPlateFadeTo - kPlateFadeFrom), 0.0f, 1.0f);
        alpha[i] = 1.0f - (1.0f - kPlateCoveredAlpha) * t * t * (3.0f - 2.0f * t);
    }
    return alpha;
}

float PlateFade::step(std::uint32_t id, float alpha_target, float dt, bool front) {
    const float goal = front ? 1.0f : std::clamp(alpha_target, 0.0f, 1.0f);
    auto [it, fresh] = entries_.try_emplace(id, Entry{goal, true});
    Entry& e = it->second;
    e.seen = true;
    if (front) e.alpha = 1.0f;
    else if (!fresh) e.alpha += (goal - e.alpha) * (1.0f - std::exp(-std::max(dt, 0.0f) / 0.3f));
    return e.alpha;
}

void PlateFade::end_frame() {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (!it->second.seen) it = entries_.erase(it);
        else {
            it->second.seen = false;
            ++it;
        }
    }
}

} // namespace nmsc::game
