// The name plates' text options (game/nameplate_style.h): their defaults, ranges, settings keys and distance shrink.

#include <doctest/doctest.h>

#include "core/config.h"
#include "game/nameplate_style.h"

using namespace hailstone;
using namespace hailstone::game;

TEST_CASE("plate style: the defaults draw names as before the options") {
    Config c;
    PlateStyle s = load_plate_style(c);
    CHECK(s.face.empty());
    CHECK(s.size == 15);
    CHECK_FALSE(s.bold);
    CHECK(s.edge == PlateEdge::Shadow);
    CHECK(s.far_scale == 100);
    CHECK(s.tag_scale == 100);
    CHECK(plate_distance_factor(250.0f, 250.0f, s.far_scale) == 1.0f);
}

TEST_CASE("plate style: stored and loaded again, out-of-range values clamped") {
    Config c;
    PlateStyle s;
    s.face = "Test Sans";
    s.size = 22;
    s.bold = true;
    s.edge = PlateEdge::Outline;
    s.far_scale = 40;
    s.tag_scale = 75;
    store_plate_style(s, c);
    PlateStyle back = load_plate_style(c);
    CHECK(back.face == "Test Sans");
    CHECK(back.size == 22);
    CHECK(back.bold);
    CHECK(back.edge == PlateEdge::Outline);
    CHECK(back.far_scale == 40);
    CHECK(back.tag_scale == 75);

    c.set_int("display.plate_font_size", 500);
    c.set_int("display.plate_edge", 9);
    c.set_int("display.plate_far_scale", 0);
    c.set_int("display.plate_tag_scale", 300);
    back = load_plate_style(c);
    CHECK(back.size == kPlateFontSizeMax);
    CHECK(back.edge == PlateEdge::Shadow);
    CHECK(back.far_scale == kPlateFarScaleMin);
    CHECK(back.tag_scale == kPlateTagScaleMax);
}

TEST_CASE("plate style: the options by name") {
    PlateStyle s;
    CHECK(set_plate_style_option(s, "plate_font_size", 2));
    CHECK(plate_style_option(s, "plate_font_size") == kPlateFontSizeMin);
    CHECK(set_plate_style_option(s, "plate_font_bold", 1));
    CHECK(plate_style_option(s, "plate_font_bold") == 1);
    CHECK(set_plate_style_option(s, "plate_edge", 2));
    CHECK(s.edge == PlateEdge::None);
    CHECK(set_plate_style_option(s, "plate_far_scale", 60));
    CHECK(plate_style_option(s, "plate_far_scale") == 60);
    CHECK(set_plate_style_option(s, "plate_tag_scale", 10));
    CHECK(plate_style_option(s, "plate_tag_scale") == kPlateTagScaleMin);
    CHECK_FALSE(set_plate_style_option(s, "plate_font_face", 1)); // the client's: its list is the system's
    CHECK_FALSE(plate_style_option(s, "pc_names"));
}

TEST_CASE("plate style: names shrink evenly to the far size at the range") {
    CHECK(plate_distance_factor(0.0f, 250.0f, 50) == 1.0f);
    CHECK(plate_distance_factor(125.0f, 250.0f, 50) == doctest::Approx(0.75f));
    CHECK(plate_distance_factor(250.0f, 250.0f, 50) == doctest::Approx(0.5f));
    CHECK(plate_distance_factor(400.0f, 250.0f, 50) == doctest::Approx(0.5f));
    CHECK(plate_distance_factor(-5.0f, 250.0f, 50) == 1.0f);
    CHECK(plate_distance_factor(100.0f, 0.0f, 50) == 1.0f);
    CHECK(plate_distance_factor(250.0f, 250.0f, 10) == doctest::Approx(0.25f)); // clamped to the minimum
}
