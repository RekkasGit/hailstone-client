// core/work_budget.h: render::ActorRenderer's per-frame time for new appearances.
#include <doctest/doctest.h>

#include "core/work_budget.h"

using hailstone::core::WorkBudget;

TEST_CASE("work budget: units start while the time spent is under the budget") {
    WorkBudget b(4.0);
    CHECK(b.may_start());
    b.done(1.5);
    CHECK(b.may_start());
    b.done(2.0);
    CHECK(b.may_start()); // 3.5 ms spent
    b.done(1.0);
    CHECK_FALSE(b.may_start()); // 4.5 ms: the rest wait for the next frame
    CHECK(b.units() == 3);
    CHECK(b.spent_ms() == doctest::Approx(4.5));
}

TEST_CASE("work budget: the first unit always starts, however slow") {
    WorkBudget b(4.0);
    b.charge(10.0); // a default mesh had to go up first
    CHECK(b.may_start());
    b.done(30.0); // one appearance far over the budget
    CHECK_FALSE(b.may_start());
    CHECK(b.units() == 1);

    WorkBudget none(0.0); // even a zero budget makes progress, one a frame
    CHECK(none.may_start());
    none.done(0.0);
    CHECK_FALSE(none.may_start());
}

TEST_CASE("work budget: a larger budget behind the loading screen builds more a frame") {
    auto per_frame = [](double budget, double each) {
        WorkBudget b(budget);
        while (b.may_start() && b.units() < 1000) b.done(each);
        return b.units();
    };
    CHECK(per_frame(4.0, 12.0) == 1);   // the open: one bazaar appearance a frame
    CHECK(per_frame(100.0, 12.0) == 9); // the loading screen: 9 x 12 = 108 ms
    CHECK(per_frame(4.0, 0.5) == 8);    // cheap ones (cached geometry, shared textures): several
}
