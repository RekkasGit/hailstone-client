#include "app/guide_path.h"

#include <cmath>
#include <limits>

namespace hailstone::app {

namespace {

float flat(const glm::vec3& a, const glm::vec3& b) { return glm::length(glm::vec2(a) - glm::vec2(b)); }

// The nearest point to `p` on the segment a-b (flat), and its distance.
glm::vec3 nearest_on(const glm::vec3& a, const glm::vec3& b, const glm::vec3& p, float& t) {
    const glm::vec2 ab = glm::vec2(b) - glm::vec2(a);
    const float l2 = glm::dot(ab, ab);
    t = l2 > 0.0f ? glm::clamp(glm::dot(glm::vec2(p) - glm::vec2(a), ab) / l2, 0.0f, 1.0f) : 0.0f;
    return a + (b - a) * t;
}

} // namespace

void GuidePath::start(std::uint32_t spawn, std::string name) {
    spawn_ = spawn;
    name_ = std::move(name);
    line_.clear();
    partial_ = false;
    told_waiting_ = false;
    next_plan_ = 0.0;
}

void GuidePath::stop() {
    spawn_ = 0;
    name_.clear();
    line_.clear();
    partial_ = false;
}

void GuidePath::trim(const glm::vec3& feet) {
    // The segment nearest the player: what is behind it goes, and the line starts at the player.
    if (line_.size() < 2) return;
    std::size_t best = 0;
    float best_d = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i + 1 < line_.size(); ++i) {
        float t = 0.0f;
        const glm::vec3 q = nearest_on(line_[i], line_[i + 1], feet, t);
        const float d = flat(q, feet);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    if (best > 0) line_.erase(line_.begin(), line_.begin() + static_cast<std::ptrdiff_t>(best));
    line_[0] = feet;
    if (line_.size() >= 2 && flat(line_[0], line_[1]) < 0.5f && line_.size() > 2) line_.erase(line_.begin() + 1);
}

GuidePath::Update GuidePath::update(const glm::vec3& feet, std::optional<glm::vec3> goal, double now,
                                    const PathFn& path) {
    Update u;
    if (!active()) return u;
    if (!goal) {
        u.gone = true;
        stop();
        return u;
    }
    if (flat(feet, *goal) <= kArrive && std::abs(feet.z - goal->z) <= 10.0f) {
        u.arrived = true;
        stop();
        return u;
    }
    // Off the line, or the NPC moved: found again (at most every kReplanGap).
    bool replan = line_.size() < 2;
    if (!replan && flat(*goal, planned_goal_) > kGoalMoved) replan = true;
    if (!replan) {
        float t = 0.0f, off = std::numeric_limits<float>::max();
        for (std::size_t i = 0; i + 1 < line_.size(); ++i)
            off = std::min(off, flat(nearest_on(line_[i], line_[i + 1], feet, t), feet));
        if (off > kStray) replan = true;
    }
    const bool held = replan && (now < next_plan_ || !path); // a search due, not yet allowed
    if (replan && !held) {
        auto r = path(feet, *goal);
        if (!r) {
            next_plan_ = now + kMeshRetry;
            if (!told_waiting_) {
                told_waiting_ = true;
                u.waiting_mesh = true;
            }
        } else if (r->size() < 2) {
            u.no_way = true;
            stop();
            return u;
        } else {
            line_ = std::move(*r);
            planned_goal_ = *goal;
            partial_ = flat(line_.back(), *goal) > kArrive;
            next_plan_ = now + kReplanGap;
        }
    }
    // Not while a search is held back: a line snapped to the player would hide that the player left it.
    if (!held) trim(feet);
    return u;
}

} // namespace hailstone::app
