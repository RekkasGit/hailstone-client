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
