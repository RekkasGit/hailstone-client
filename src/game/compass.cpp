#include "game/compass.h"

#include <cstdlib>

namespace hailstone::game {

void CompassError::pick_target(std::int32_t skill, const Rand& rand) {
    if (skill < 0) skill = 1; // no character
    if (skill >= 200) {
        target_ = 0;
        return;
    }
    const std::int32_t range = (200 - skill) * 512 / 200;
    const std::int32_t r = (rand ? rand() : 0) % range;
    target_ = r - range / 2;
    const std::int32_t least = 51 - skill / 5;
    if (std::abs(target_) < least) target_ = (target_ >= 0 ? 1 : -1) * least;
}

void CompassError::update_rate() {
    rate_ = 80.0;
    if (std::abs(target_ - error_) > 50 || hold_ > 0) rate_ = 160.0;
    if (error_ > target_) rate_ = -rate_;
}

void CompassError::process(std::uint32_t now_ms, bool shown, std::int32_t skill, const Rand& rand) {
    if (shown && !was_shown_) pick_target(skill, rand);
    was_shown_ = shown;
    if (shown && skill >= 0 && hold_ <= 0) {
        if (skill >= 200) {
            error_ = 0;
        } else {
            const auto elapsed = static_cast<std::int32_t>(now_ms - last_);
            const auto step = static_cast<std::int32_t>(static_cast<double>(elapsed) * rate_ * 0.001);
            if (std::abs(target_ - error_) < step) {
                error_ = target_;
                pick_target(skill, rand);
            } else {
                error_ += step;
                if (error_ > 256) error_ -= 512;
                else if (error_ < -256) error_ += 512;
            }
            update_rate();
        }
    }
    // (wrapping as the original's 32-bit add does)
    hold_ = static_cast<std::int32_t>(static_cast<std::uint32_t>(hold_) + (last_ - now_ms));
    last_ = now_ms;
}

std::int32_t CompassError::heading(float heading) const {
    return (static_cast<std::int32_t>(heading) + error_) % 512;
}

} // namespace hailstone::game
