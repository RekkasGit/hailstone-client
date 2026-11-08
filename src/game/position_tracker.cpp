#include "game/position_tracker.h"

#include "core/byte_io.h"

namespace hailstone::game {

void PositionTracker::enable() {
    if (!enabled_) flush();
    enabled_ = true;
}

bool PositionTracker::record(const std::array<float, 3>& p, std::uint32_t reason, std::uint32_t now, const Gate& g) {
    if (!enabled_ || !g.in_game || !g.zone_connected || !g.controlled) return false;
    if (!g.main_loop) {
        if (reason == kPositionMove) return false;
    } else if (reason == kPositionMove) {
        // FUN_0056e250: fucompp per component, a NaN never equal.
        bool same = has_last_ && p[0] == last_[0] && p[1] == last_[1] && p[2] == last_[2];
        if (same && now - last_time_ < 1000) return false;
        last_ = p;
        has_last_ = true;
    }
    samples_.push_back({p, reason, now});
    ++sequence_;
    last_time_ = now;
    return reason == 3 || reason == 4 || reason == 5 || reason == 8; // FUN_0056e290
}

std::vector<std::byte> PositionTracker::take(bool force, std::uint32_t now) {
    ByteWriter w;
    std::size_t count = 0;
    if (!enabled_) return {};
    while (!samples_.empty() && count < 10000) {
        const Sample& s = samples_.front();
        if (!force && now - s.time <= 2000) break;
        // The jump table on the reason sends 1..9 alike; 6 throws (never recorded).
        w.f32(s.position[0]);
        w.f32(s.position[1]);
        w.f32(s.position[2]);
        w.le(static_cast<std::uint8_t>(s.reason));
        w.le(static_cast<std::uint16_t>(s.time));
        w.le(entries_++);
        samples_.pop_front();
        ++count;
    }
    if (count == 0) return {};
    w.le(std::uint8_t{0});
    return w.take();
}

void PositionTracker::flush() {
    samples_.clear();
    sequence_ = entries_;
}

} // namespace hailstone::game
