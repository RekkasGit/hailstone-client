#pragma once

// A frame's time budget for work that can wait for a later frame (render::ActorRenderer's new appearances): units of
// work start while the time spent this frame is under the budget, and the first one always starts, so the work
// makes progress however slow one unit is. The caller measures each unit and reports it (no clock in here, so a test
// drives it with numbers).

#include <cstddef>

namespace hailstone::core {

class WorkBudget {
public:
    explicit WorkBudget(double budget_ms) : budget_ms_(budget_ms) {}

    // Whether another unit may start this frame.
    bool may_start() const { return started_ == 0 || spent_ms_ < budget_ms_; }
    // A unit done, and what it took.
    void done(double ms) {
        ++started_;
        spent_ms_ += ms;
    }
    // Time the frame had to spend anyway on the same kind of work (it counts against the budget, but is not a unit:
    // the first unit still starts).
    void charge(double ms) { spent_ms_ += ms; }

    std::size_t units() const { return started_; }
    double spent_ms() const { return spent_ms_; }
    double budget_ms() const { return budget_ms_; }

private:
    double budget_ms_;
    double spent_ms_ = 0.0;
    std::size_t started_ = 0;
};

} // namespace hailstone::core
