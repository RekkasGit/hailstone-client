// The compass's heading error (CCompassWnd @0x65b5f0..0x65c100, game/compass.h).
#include <doctest/doctest.h>

#include "game/compass.h"

using nmsc::game::CompassError;

TEST_CASE("compass: the error's target from the Sense Heading skill") {
    CompassError c;
    // skill 100: range (200 - 100) * 512 / 200 = 256, target rand % 256 - 128, at least 51 - 20 = 31 either way
    c.process(1000, true, 100, [] { return 300; }); // shown: 300 % 256 - 128 = -84
    CHECK(c.target() == -84);
    CompassError z;
    z.process(1000, true, 100, [] { return 128; }); // 0: pushed to +31
    CHECK(z.target() == 31);
    CompassError n;
    n.process(1000, true, 100, [] { return 120; }); // -8: pushed to -31
    CHECK(n.target() == -31);
    CompassError m;
    m.process(1000, true, 200, [] { return 7; }); // 200 and up: no error
    CHECK(m.target() == 0);
    CHECK(m.error() == 0);
}

TEST_CASE("compass: the error drifts to its target, then picks another") {
    CompassError c;
    int calls = 0;
    auto rand = [&] { return calls++ == 0 ? 300 : 0; }; // -84, then 0 - 128 = -128
    c.process(0, true, 100, rand);    // shown: target -84; error moves by trunc(0 * rate) = 0
    CHECK(c.target() == -84);
    CHECK(c.rate() == -160.0);        // more than 50 away, error above the target
    c.process(100, true, 100, rand);  // trunc(100 * -160 * 0.001) = -16
    CHECK(c.error() == -16);
    CHECK(c.rate() == -160.0);        // 68 away
    c.process(200, true, 100, rand);
    CHECK(c.error() == -32);
    c.process(300, true, 100, rand);  // -48: 36 away, the slow rate
    CHECK(c.error() == -48);
    CHECK(c.rate() == -80.0);
    // a negative step never counts as reaching it: the error passes the target, the rate turns, and a positive
    // step longer than the distance lands on it and picks a new target
    c.process(800, true, 100, rand);  // -48 - 40 = -88
    CHECK(c.error() == -88);
    CHECK(c.rate() == 80.0);
    c.process(900, true, 100, rand);  // step 8 > 4: on the target, the next target -128
    CHECK(c.error() == -84);
    CHECK(c.target() == -128);
    CHECK(c.heading(10.5f) == (10 - 84) % 512); // a C remainder: -74
}

TEST_CASE("compass: Sense Heading holds the error at 0 for 5 s") {
    CompassError c;
    c.process(0, true, 50, [] { return 0; });
    c.process(1000, true, 50, [] { return 0; });
    REQUIRE(c.error() != 0);
    c.sense_heading();
    CHECK(c.error() == 0);
    CHECK(c.hold() == 5000);
    c.process(4000, true, 50, [] { return 0; }); // 3000 ms gone, still held
    CHECK(c.error() == 0);
    CHECK(c.hold() == 2000);
    c.process(5000, false, 50, [] { return 0; }); // hidden: the hold still counts down
    CHECK(c.hold() == 1000);
    c.process(6000, true, 50, [] { return 0; }); // (shown again: a new target; held until this frame's count)
    CHECK(c.hold() == 0);
    c.process(6100, true, 50, [] { return 0; }); // free: the fast rate while the hold was on, now by distance
    CHECK(c.error() != 0);
}
