#include <doctest/doctest.h>

#include "assets/sky.h"
#include "assets/sky_weather.h"
#include "core/ini_file.h"
#include "game/cloud_patches.h"
#include <cmath>
#include <numbers>
#include "game_files.h"

using namespace nmsc::assets;

TEST_CASE("Sky colour entries follow the dome height") {
    CHECK(sky_color_entry(-0.2f) == 16);
    CHECK(sky_color_entry(0.0f) == 16);
    CHECK(sky_color_entry(0.0078f) == 16); // below 1/128
    CHECK(sky_color_entry(0.0860f) == 5);  // LAYER11's top: 16 - 11
    CHECK(sky_color_entry(0.125f) == 0);
    CHECK(sky_color_entry(0.5f) == 0);
}

TEST_CASE("Sky colour table: day, night and byte order") {
    // Level 14 entry 0 is stored as bytes f7 ef 9f 00 (B, G, R): a light blue.
    glm::vec3 day = sky_table_color(14, 0);
    CHECK(day.r == doctest::Approx(0x9f / 255.0));
    CHECK(day.g == doctest::Approx(0xef / 255.0));
    CHECK(day.b == doctest::Approx(0xf7 / 255.0));
    // Level 44 entry 0 is 27 00 00 00: a dark blue.
    glm::vec3 night = sky_table_color(44, 0);
    CHECK(night.r == 0.0f);
    CHECK(night.g == 0.0f);
    CHECK(night.b == doctest::Approx(0x27 / 255.0));
    CHECK(sky_table_color(14, 17).r == doctest::Approx(0x50 / 255.0));
}

TEST_CASE("Sky level over the day") {
    CHECK(sky_level(0.0f) == 44.0f);
    CHECK(sky_level(2.9f) == 44.0f);
    CHECK(sky_level(3.0f) == doctest::Approx(44.0));   // dawn starts
    CHECK(sky_level(6.0f) == doctest::Approx(59.0));   // q = 30
    CHECK(sky_level(6.2f) == doctest::Approx(0.0));    // q = 31 wraps
    CHECK(sky_level(8.99f) == doctest::Approx(13.95).epsilon(0.01));
    CHECK(sky_level(12.0f) == 14.0f);
    CHECK(sky_level(15.0f) == doctest::Approx(14.0));  // dusk starts
    CHECK(sky_level(18.0f) == doctest::Approx(29.0));
    CHECK(sky_level(21.0f) == 44.0f);
    CHECK(sky_light_level(14.0f) == 1.0f);
    CHECK(sky_light_level(44.0f) == 0.0f);
    CHECK(sky_light_level(sky_level(12.0f)) == 1.0f);
}

TEST_CASE("Ambient per hour and the sweep heading") {
    glm::vec3 noon = sky_ambient(12.0f);
    CHECK(noon.r == doctest::Approx(0.9));
    CHECK(noon.b == doctest::Approx(0.9));
    glm::vec3 night = sky_ambient(1.0f);
    CHECK(night.r == doctest::Approx(0.08)); // floor
    CHECK(night.b == doctest::Approx(0.3));
    CHECK(sky_ambient(0.0f).b == doctest::Approx(0.3)); // hour 0 = 24
    CHECK(sky_ambient(4.5f).r == doctest::Approx(0.225)); // between 0.1 and 0.35
    CHECK(sky_sweep_heading(9.0f) == doctest::Approx(448.0));
    CHECK(sky_sweep_heading(12.0f) == doctest::Approx(0.0).epsilon(0.001));
    CHECK(sky_sweep_heading(15.0f) == doctest::Approx(64.0));
    CHECK(sky_sweep_heading(21.0f) == doctest::Approx(192.0));
    CHECK(sky_sweep_heading(3.0f) == doctest::Approx(320.0));
}

TEST_CASE("sky.s3d: layers per sky index" * doctest::skip(!nmsc::test::game_dir())) {
    const auto& game = *nmsc::test::game_dir();
    CHECK_FALSE(load_sky(game, 0));
    CHECK_FALSE(load_sky(game, kMaxSkyIndex + 1));

    auto normal = load_sky(game, 1);
    REQUIRE(normal);
    REQUIRE(normal->dome);
    REQUIRE(normal->clouds);
    REQUIRE(normal->bodies);
    CHECK(normal->dome->mesh.vertices.size() == 145);
    CHECK(normal->dome->mesh.indices.size() == 264 * 3);
    CHECK(normal->dome->color_entry.size() == 145);
    REQUIRE(normal->dome->mesh.materials.size() == 1);
    CHECK(normal->dome->mesh.materials[0].frames.front() == "NORMALSKY.BMP");
    CHECK(normal->clouds->mesh.materials[0].frames.front() == "NORMALCLOUD.BMP");
    CHECK(normal->bodies->materials.size() == 2); // moon and sun
    CHECK(normal->read_file("normalsky.bmp"));

    for (int i = 1; i <= kMaxSkyIndex; ++i) {
        auto s = load_sky(game, i);
        CAPTURE(i);
        REQUIRE(s);
        CHECK(s->dome);
        CHECK(s->clouds);
        CHECK(static_cast<bool>(s->bodies) == (i == 1 || i == 3 || i == 6));
    }
}

TEST_CASE("procedural sky clouds: weather.ini definitions and patch motion") {
    nmsc::IniFile sky, weather;
    sky.parse("[SkySetting-default]\nDefaultWeather=DefaultClear\n[SkySetting-moors]\nDefaultWeather=OmensClear\n");
    weather.parse("[WeatherPattern-DefaultClear]\nCloud0=Clear0\nCloud1=Clear1\n"
                  "[Cloud-Clear0]\nTexture=DefaultOrographic\nDesiredCount=4\nVelocityMin=0.0000008\n"
                  "VelocityMax=0.000005\nSpinMin=-0.000005\nSpinMax=0.000005\nRotationMax=6.283\nSizeMin=0.3\n"
                  "SizeMax=0.5\nVisibleAngle=1.36\nElevation=99\nRadius=101\nSubdivisionX=5\nSubdivisionY=5\n"
                  "[Cloud-Clear1]\nTexture=DefaultStratocumulus\nDesiredCount=0\n");
    CHECK(nmsc::assets::default_weather(sky, "qeynos2") == "DefaultClear");
    CHECK(nmsc::assets::default_weather(sky, "moors") == "OmensClear");
    auto clouds = nmsc::assets::weather_clouds(weather, "DefaultClear");
    REQUIRE(clouds.size() == 1); // Clear1 has no patches
    const auto& d = clouds[0];
    CHECK(d.texture == "DefaultOrographic");
    CHECK(d.desired_count == 4);
    CHECK(d.velocity_max == doctest::Approx(0.000005f));
    CHECK(d.visible_angle == doctest::Approx(1.36f));
    CHECK(d.subdivision_x == 5);
    {
        nmsc::IniFile w;
        w.parse("[Cloud-X]\nDesiredCount=-1e30\nSubdivisionX=1e30\nSubdivisionY=nan\n");
        auto x = nmsc::assets::parse_cloud_definition(w, "X"); // clamped, not cast out of range
        CHECK(x.desired_count == -2147483647 - 1);
        CHECK(x.subdivision_x == 2147483647);
        CHECK(x.subdivision_y == 0);
        nmsc::IniFile big;
        big.parse("[Cloud-X]\nDesiredCount=2000000000\n");
        CHECK(nmsc::assets::parse_cloud_definition(big, "X").desired_count == nmsc::assets::kMaxCloudPatches);
    }

    // rand() / 32767 x (max - min) + min.
    CHECK(nmsc::game::cloud_random(0, 2.0f, 4.0f) == 2.0f);
    CHECK(nmsc::game::cloud_random(32767, 2.0f, 4.0f) == 4.0f);

    // A fill spreads n patches over the visible arc: i (2 VA / n) + (pi/2 - VA).
    int r = 32767;
    nmsc::game::CloudLayer layer(d, [&] { return r; });
    layer.fill();
    REQUIRE(layer.patches().size() == 4);
    const float start = std::numbers::pi_v<float> / 2.0f - 1.36f;
    CHECK(layer.patches()[0].angle == doctest::Approx(start));
    CHECK(layer.patches()[3].angle == doctest::Approx(start + 3.0f * 2.0f * 1.36f / 4.0f));
    // Velocity and Spin are radians per ms: angle += v dt (unwrapped),
    // rotation = fmod(rotation + spin dt, 2 pi).
    nmsc::game::CloudPatch p;
    p.angle = 6.0f;
    p.velocity = 0.000005f;
    p.rotation = 6.0f;
    p.spin = 0.001f;
    nmsc::game::advance_cloud_patch(p, 1000.0f);
    CHECK(p.angle == doctest::Approx(6.005f)); // 0.005 rad per second
    CHECK(p.rotation == doctest::Approx(std::fmod(7.0f, 6.2831854f)));
    nmsc::game::advance_cloud_patch(p, 1.0e6f);
    CHECK(p.angle > 6.2831854f); // not wrapped
    // Without a Duration a patch never fades.
    CHECK(nmsc::game::CloudLayer::opacity(p) == 1.0f);
}
