#include <doctest/doctest.h>

#include <limits>

#include "game/scene_light.h"
#include "render/zone_renderer.h"

using namespace hailstone::game;

TEST_CASE("CMath tables: exact quadrants, truncated and interpolated lookups") {
    CHECK(cmath_sin(0.0f) == 0.0f);
    CHECK(cmath_sin(128.0f) == 1.0f);
    CHECK(cmath_sin(384.0f) == -1.0f);
    CHECK(cmath_cos(256.0f) == -1.0f);
    CHECK(cmath_cos(128.0f) == 0.0f);
    // +0x08: truncated, masked, no interpolation; 511.9 uses entry 511, -1.5 entry 511.
    CHECK(cmath_sin(511.9f) == cmath_sin(511.0f));
    CHECK(cmath_sin(-1.5f) == cmath_sin(511.0f));
    CHECK(cmath_sin(64.0f) == doctest::Approx(0.70710678));
    // NaN, infinities and values past int32 (a forged .pts rotation) take entry 0, as cvttss2si's INT_MIN does.
    CHECK(cmath_sin(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    CHECK(cmath_cos(std::numeric_limits<float>::infinity()) == 1.0f);
    CHECK(cmath_sin(-1e20f) == 0.0f);
    CHECK(cmath_cos(3e9f) == 1.0f);
    // +0x00 interpolates; the last entry wraps to entry 0.
    CHECK(cmath_sin_interp(64.5f) == doctest::Approx((cmath_sin(64.0f) + cmath_sin(65.0f)) / 2.0));
    CHECK(cmath_sin_interp(511.5f) == doctest::Approx(cmath_sin(511.0f) / 2.0));
    CHECK(cmath_cos_interp(0.25f) == doctest::Approx(1.0 + (cmath_cos(1.0f) - 1.0) * 0.25));
}

TEST_CASE("Sun direction: straight down at noon, horizontal at 18:00") {
    // Sweep heading 0 at noon (assets::sky_sweep_heading): Dn = (0, sin 512, -cos 512).
    auto noon = sky_light_direction(0.0f);
    CHECK(noon.x == 0.0f);
    CHECK(noon.y == doctest::Approx(0.0));
    CHECK(noon.z == doctest::Approx(-1.0));
    auto dusk = sky_light_direction(128.0f);
    CHECK(dusk.y == doctest::Approx(-1.0));
    CHECK(dusk.z == doctest::Approx(0.0).epsilon(1e-6));
}

TEST_CASE("IsNight: before 5:55 and after 18:20 of the day fraction") {
    CHECK(sky_is_night(5.9f));
    CHECK_FALSE(sky_is_night(6.0f));
    CHECK_FALSE(sky_is_night(18.3f));
    CHECK(sky_is_night(18.4f));
}

TEST_CASE("Scene light: ambient by time type, the vision floor and SpecialAmbient") {
    SceneLightInput in;
    in.time_type = 0; // dungeon: 0.1 grey
    auto d = compute_scene_light(in);
    CHECK(d.ambient.r == doctest::Approx(25.0 / 255.0)); // trunc(0.1 * 255) = 25
    CHECK(d.directional == glm::vec3(0.0f));
    CHECK(d.special_ambient == glm::vec3(0.0f)); // L = 1: no floor term
    in.time_type = 4;
    CHECK(compute_scene_light(in).ambient.g == doctest::Approx(127.0 / 255.0));

    in.time_type = 1;
    in.hour = 1.0f; // the night entry: (0, 0, 0.3), raised to the 0.08 floor
    auto n = compute_scene_light(in);
    CHECK(n.ambient.r == doctest::Approx(20.0 / 255.0));
    CHECK(n.ambient.b == doctest::Approx(76.0 / 255.0));
    in.hour = 12.0f; // noon: 0.9 grey, sun overhead at full light level
    auto day = compute_scene_light(in);
    CHECK(day.ambient.r == doctest::Approx(229.0 / 255.0));
    CHECK(day.direction.z == doctest::Approx(-1.0));
    CHECK(day.directional.r == doctest::Approx(1.0));

    in.player_light = 0.25f;
    in.global_ambient = glm::vec3(0.5f, 0.0f, 1.0f);
    auto s = compute_scene_light(in);
    CHECK(s.special_ambient.r == doctest::Approx(15.0 / 255.0 + 0.5)); // trunc(0.08 * 0.75 * 255) = 15
    CHECK(s.special_ambient.b == 1.0f);                               // clamped to 1
}

TEST_CASE("point light term of the region shaders (MPL path)") {
    using hailstone::game::point_light_term;
    const glm::vec3 up(0, 0, 1);
    // Straight above at half the radius: facing 1, falloff 1 - 0.25.
    glm::vec3 c = point_light_term({0, 0, 50}, {0.5f, 0.4f, 0.2f}, 100.0f, {0, 0, 0}, up);
    CHECK(c.x == doctest::Approx(0.375f));
    CHECK(c.z == doctest::Approx(0.15f));
    // At 45 degrees: facing cos 45.
    c = point_light_term({50, 0, 50}, {1, 1, 1}, 100.0f, {0, 0, 0}, up);
    CHECK(c.x == doctest::Approx(0.70710678f * 0.5f));
    // Behind the surface, beyond the radius, or no radius: nothing.
    CHECK(point_light_term({0, 0, -10}, {1, 1, 1}, 100.0f, {0, 0, 0}, up) == glm::vec3(0.0f));
    CHECK(point_light_term({0, 0, 101}, {1, 1, 1}, 100.0f, {0, 0, 0}, up) == glm::vec3(0.0f));
    CHECK(point_light_term({0, 0, 1}, {1, 1, 1}, 0.0f, {0, 0, 0}, up) == glm::vec3(0.0f));
}

TEST_CASE("EQG effect time and the MultiPassLighting setting") {
    // a_fTime = (T mod 100000) x 0.001 s; k = fmod(a_fTime, 100).
    CHECK(hailstone::render::effect_scroll_seconds(0) == 0.0f);
    CHECK(hailstone::render::effect_scroll_seconds(12345) == doctest::Approx(12.345f));
    CHECK(hailstone::render::effect_scroll_seconds(100000) == 0.0f);
    CHECK(hailstone::render::effect_scroll_seconds(1234567) == doctest::Approx(34.567f));
    // First character t/T/1 on, anything else off (CRender::ReadIniRenderSettings).
    CHECK(hailstone::render::multipass_lighting_setting("1"));
    CHECK(hailstone::render::multipass_lighting_setting("TRUE"));
    CHECK(hailstone::render::multipass_lighting_setting("true"));
    CHECK_FALSE(hailstone::render::multipass_lighting_setting("0"));
    CHECK_FALSE(hailstone::render::multipass_lighting_setting("FALSE"));
    CHECK_FALSE(hailstone::render::multipass_lighting_setting(""));
    CHECK_FALSE(hailstone::render::multipass_lighting_setting("yes"));
}

#include "game/scaled_ambient.h"
#include "game/scene_fog.h"

TEST_CASE("GetScaledAmbient: the level follows the probed target at 0.004 per ms") {
    hailstone::game::ScaledAmbient a;
    // No collision: every probe misses, the target is the 0.1 floor (probed once more than 1000 ms in).
    CHECK(a.update(500, glm::vec3(0.0f), nullptr) == 1.0f);
    float l = a.update(1500, glm::vec3(0.0f), nullptr);
    CHECK(a.target() == doctest::Approx(0.1f));
    CHECK(l == doctest::Approx(0.1f)); // 1000 ms since the last update: t >= 1
    a.reset();
    a.update(1001, glm::vec3(0.0f), nullptr); // probes: target 0.1, last update 0 -> t = 4: at the target
    CHECK(a.update(1002, glm::vec3(0.0f), nullptr) == doctest::Approx(0.1f));
}

TEST_CASE("Scene fog per frame: region fog, the fog-off zone and the sky flag") {
    using namespace hailstone::game;
    ZoneFogInput z;
    z.fog_enabled = true;
    z.fog_color = glm::vec3(230.0f, 255.0f, 200.0f) / 255.0f;
    z.fog_minclip = 10.0f;
    z.fog_maxclip = 1600.0f;
    z.minclip = 50.0f;
    z.maxclip = 1600.0f;
    z.fog_density = 0.33f;
    z.time_type = 2;
    SceneFog fog;
    fog.set_zone(z, true);
    SceneFogInput in;
    in.hour = 12;
    in.clip_plane = 15;
    auto f = fog.frame(in);
    // far = 50 + 15 x 0.05 x 1550 = 1212.5; end = far - 0.15 (far - 10) = 1032.125.
    CHECK(f.view.far_plane == doctest::Approx(1212.5));
    CHECK(f.view.fog_start == 10.0f);
    CHECK(f.view.fog_end == 1032.0f);
    CHECK(f.view.fog_density == doctest::Approx(0.33f));
    CHECK(f.sky);
    // Noon ambient 0.9: each fog channel trunc(trunc(0.9 K) x 0.9 x 1).
    CHECK(f.view.fog_color.r == doctest::Approx(186.0 / 255.0));
    CHECK(f.view.fog_color.g == doctest::Approx(206.0 / 255.0));
    CHECK(f.view.fog_color.b == doctest::Approx(162.0 / 255.0));
    // Under water: 30..800, the water colour, and no sky.
    in.camera_region = 5;
    f = fog.frame(in);
    CHECK(f.view.fog_start == 30.0f);
    CHECK(f.view.fog_end == 800.0f);
    CHECK(f.view.fog_color.g == 1.0f); // trunc(0.9 x 570) x 0.9 = 461: clamped to 255
    CHECK_FALSE(f.sky);
    // ztype 0: fog off (no density), the sky back on above water.
    z.fog_enabled = false;
    fog.set_zone(z, true);
    in.camera_region = 0;
    f = fog.frame(in);
    CHECK(f.view.fog_density == 0.0f);
    CHECK(f.sky);
}
