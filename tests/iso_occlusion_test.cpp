// Seeing the player through what stands in front, in the isometric camera (game/iso_occlusion.h).
#include <doctest/doctest.h>

#include "game/iso_occlusion.h"

using namespace hailstone::game;

namespace {
// The camera 100 back along -y and 100 up, looking at a player whose middle is at (0, 0, 4), feet at 0.
IsoReveal reveal(float strength = 1.0f) {
    IsoRevealInput in;
    in.camera = {0.0f, -100.0f, 104.0f};
    in.focus = {0.0f, 0.0f, 4.0f};
    in.feet_z = 0.0f;
    in.radius = 12.0f;
    in.strength = strength;
    return iso_reveal(in);
}
} // namespace

TEST_CASE("iso reveal: a wall between the camera and the player is cut") {
    IsoReveal r = reveal();
    // On the line to the player, halfway: well above the feet, well nearer.
    CHECK(iso_reveal_cut(r, {0.0f, -50.0f, 54.0f}) == doctest::Approx(1.0f));
    // A wall just in front of the player, at head height.
    CHECK(iso_reveal_cut(r, {0.0f, -8.0f, 12.0f}) == doctest::Approx(1.0f));
}

TEST_CASE("iso reveal: the ground, the player and what lies behind are kept") {
    IsoReveal r = reveal();
    CHECK(iso_reveal_cut(r, {0.0f, -4.0f, 0.0f}) == 0.0f);   // the floor in front of the feet
    CHECK(iso_reveal_cut(r, {0.0f, -4.0f, 0.5f}) == 0.0f);   // a kerb below the floor margin
    CHECK(iso_reveal_cut(r, {0.0f, 0.0f, 4.0f}) == 0.0f);    // the player's own middle
    CHECK(iso_reveal_cut(r, {0.0f, 10.0f, 8.0f}) == 0.0f);   // behind the player
    CHECK(iso_reveal_cut(r, {0.0f, -120.0f, 130.0f}) == 0.0f); // behind the camera
}

TEST_CASE("iso reveal: only inside the circle round the player, softly at its edge") {
    IsoReveal r = reveal();
    // Halfway to the player the cone is half as wide.
    CHECK(iso_reveal_cut(r, {30.0f, -50.0f, 54.0f}) == 0.0f);
    CHECK(iso_reveal_cut(r, {1.5f, -50.0f, 54.0f}) == doctest::Approx(1.0f)); // 3 at the player: inside the full cut
    const float edge = iso_reveal_cut(r, {5.5f, -50.0f, 54.0f}); // 11 at the player's depth: in the soft band
    CHECK(edge > 0.0f);
    CHECK(edge < 1.0f);
}

TEST_CASE("iso reveal: it fades in above the feet and short of the player") {
    IsoReveal r = reveal();
    // Halfway up the rise above the floor margin (feet 0, floor 1, rise 3): partly cut.
    const float rising = iso_reveal_cut(r, {0.0f, -10.0f, 2.5f});
    CHECK(rising > 0.0f);
    CHECK(rising < 1.0f);
    // Just inside the depth margin's fade, on the line: partly cut.
    const float d = r.axis.w - r.shape.z - kIsoRevealDepthFade * 0.5f;
    const glm::vec3 near_player = glm::vec3(r.origin) + glm::vec3(r.axis) * d;
    const float fading = iso_reveal_cut(r, near_player);
    CHECK(fading > 0.0f);
    CHECK(fading < 1.0f);
}

TEST_CASE("iso reveal: off and partial strength") {
    CHECK(iso_reveal_cut(reveal(0.0f), {0.0f, -50.0f, 54.0f}) == 0.0f);
    CHECK(iso_reveal_cut(reveal(0.5f), {0.0f, -50.0f, 54.0f}) == doctest::Approx(0.5f));
    IsoRevealInput none;
    none.camera = none.focus = {1.0f, 2.0f, 3.0f};
    CHECK(iso_reveal(none).origin.w == 0.0f);
}

namespace {
// The same camera and player, the cone off and the roof on: cut above 9 within 60 (flat) of the player.
IsoReveal roof_only() {
    IsoRevealInput in;
    in.camera = {0.0f, -100.0f, 104.0f};
    in.focus = {0.0f, 0.0f, 4.0f};
    in.strength = 0.0f;
    in.roof_height = 9.0f;
    in.roof_radius = 60.0f;
    in.roof_strength = 1.0f;
    return iso_reveal(in);
}
} // namespace

TEST_CASE("iso reveal: the roof takes what is over the player's head round them") {
    IsoReveal r = roof_only();
    REQUIRE(r.roof.w == 1.0f);
    CHECK(r.origin.w == 0.0f); // the cone is off, the shape is still placed
    CHECK(iso_reveal_cut(r, {0.0f, 0.0f, 20.0f}) == doctest::Approx(1.0f));   // the ceiling over the player
    CHECK(iso_reveal_cut(r, {30.0f, 20.0f, 15.0f}) == doctest::Approx(1.0f)); // a wall's top, behind too
    CHECK(iso_reveal_cut(r, {0.0f, 0.0f, 5.0f}) == 0.0f);   // below the cut: walls at body height stay
    CHECK(iso_reveal_cut(r, {100.0f, 0.0f, 20.0f}) == 0.0f); // outside its circle
    const float edge = iso_reveal_cut(r, {52.0f, 0.0f, 20.0f}); // in its soft edge (45..60)
    CHECK(edge > 0.0f);
    CHECK(edge < 1.0f);
}

TEST_CASE("iso reveal: the cone and the roof together") {
    IsoRevealInput in;
    in.camera = {0.0f, -100.0f, 104.0f};
    in.focus = {0.0f, 0.0f, 4.0f};
    in.radius = 12.0f;
    in.strength = 1.0f;
    in.roof_height = 9.0f;
    in.roof_strength = 0.5f;
    IsoReveal r = iso_reveal(in);
    CHECK(iso_reveal_cut(r, {0.0f, -50.0f, 54.0f}) == doctest::Approx(1.0f)); // the cone's, the larger
    CHECK(iso_reveal_cut(r, {30.0f, 20.0f, 15.0f}) == doctest::Approx(0.5f)); // only the roof's, at half
}

TEST_CASE("iso reveal: strengths ease in fast and out slowly") {
    CHECK(iso_ease(0.0f, 1.0f, kIsoEaseIn * 0.5f) == doctest::Approx(0.5f));
    CHECK(iso_ease(0.9f, 1.0f, 1.0f) == 1.0f);
    CHECK(iso_ease(1.0f, 0.0f, kIsoEaseOut * 0.5f) == doctest::Approx(0.5f));
    CHECK(iso_ease(0.1f, 0.0f, 1.0f) == 0.0f);
    CHECK(iso_ease(0.3f, 1.0f, 0.0f) == 0.3f);
}

TEST_CASE("iso reveal: a click goes through a triangle where it is cut") {
    IsoRevealInput in;
    in.camera = {0.0f, -100.0f, 104.0f};
    in.focus = {0.0f, 0.0f, 4.0f};
    in.radius = 12.0f;
    IsoReveal r = iso_reveal(in);
    // The pick segment from the camera through the player and on.
    PickSegment s;
    s.start = in.camera;
    s.dir = (in.focus - in.camera) * 2.0f;
    // A wall across the line halfway, well above the feet: cut there, skipped.
    PickTriangle wall{{-20.0f, -50.0f, 0.0f}, {20.0f, -50.0f, 0.0f}, {0.0f, -50.0f, 200.0f}, {0.0f, -1.0f, 0.0f}, 0};
    CHECK(iso_pick_skips(r, s, wall));
    // The floor under the player: not cut, kept.
    PickTriangle floor{{-50.0f, -50.0f, 0.0f}, {50.0f, -50.0f, 0.0f}, {0.0f, 50.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 0};
    CHECK_FALSE(iso_pick_skips(r, s, floor));
    // A triangle the segment misses is never skipped.
    PickTriangle away{{500.0f, 0.0f, 0.0f}, {510.0f, 0.0f, 0.0f}, {505.0f, 0.0f, 10.0f}, {0.0f, -1.0f, 0.0f}, 0};
    CHECK_FALSE(iso_pick_skips(r, s, away));
    // With the cut off, nothing is skipped.
    CHECK_FALSE(iso_pick_skips(IsoReveal{}, s, wall));
}

TEST_CASE("iso reveal: a wide roof softens over a quarter of its radius") {
    IsoRevealInput in;
    in.camera = {0.0f, -100.0f, 104.0f};
    in.focus = {0.0f, 0.0f, 4.0f};
    in.strength = 0.0f;
    in.roof_height = 9.0f;
    in.roof_radius = 200.0f;
    in.roof_strength = 1.0f;
    IsoReveal r = iso_reveal(in);
    CHECK(r.roof.z == doctest::Approx(50.0f));
    CHECK(iso_reveal_cut(r, {140.0f, 0.0f, 30.0f}) == doctest::Approx(1.0f)); // a hill well round the player
    CHECK(iso_reveal_cut(r, {260.0f, 0.0f, 30.0f}) == 0.0f);
}
