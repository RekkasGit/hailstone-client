// render/shadows.h: the cascades' splits, coverage and texel snapping.
#include <doctest/doctest.h>

#include "render/cover_map.h"
#include "render/scene_lighting.h"
#include "render/shadows.h"
#include "game/sky_clock.h"

using namespace nmsc::render;

namespace {

ShadowCamera camera_at(glm::vec3 p) {
    ShadowCamera c;
    c.position = p;
    c.forward = {1, 0, 0};
    c.right = {0, -1, 0};
    c.up = {0, 0, 1};
    c.tan_half_x = 1.0f;
    c.tan_half_y = 0.6f;
    c.near_plane = 1.0f;
    return c;
}

glm::vec3 clip(const glm::mat4& m, glm::vec3 p) {
    glm::vec4 c = m * glm::vec4(p, 1.0f);
    return glm::vec3(c) / c.w;
}

} // namespace

TEST_CASE("shadow cascades: increasing splits ending at the distance, each slice inside its cascade") {
    glm::vec3 to_light = glm::normalize(glm::vec3(0.3f, -0.4f, 0.8f));
    auto cam = camera_at({100, 200, 30});
    auto s = fit_shadow_cascades(cam, to_light, 1200.0f, 1500.0f, 2048);
    CHECK(s.splits.x > 1.0f);
    CHECK(s.splits.x < s.splits.y);
    CHECK(s.splits.y < s.splits.z);
    CHECK(s.splits.z < s.splits.w);
    CHECK(s.splits.w == doctest::Approx(1200.0f));
    CHECK(s.texel_world.x < s.texel_world.w);
    float prev = cam.near_plane;
    for (int c = 0; c < 4; ++c) {
        // Points on the slice's edges land inside the cascade, depth in 0..1.
        for (float d : {prev, s.splits[c]})
            for (float sx : {-1.0f, 1.0f})
                for (float sy : {-1.0f, 1.0f}) {
                    glm::vec3 p = cam.position + cam.forward * d + cam.right * (sx * d) + cam.up * (sy * d * 0.6f);
                    glm::vec3 q = clip(s.view_proj[static_cast<std::size_t>(c)], p);
                    CHECK(std::abs(q.x) <= 1.0f);
                    CHECK(std::abs(q.y) <= 1.0f);
                    CHECK(q.z >= 0.0f);
                    CHECK(q.z <= 1.0f);
                }
        prev = s.splits[c];
    }
    // A caster 1000 units towards the light from the first slice is still in front of the near plane.
    glm::vec3 caster = cam.position + cam.forward * 3.0f + to_light * 1000.0f;
    CHECK(clip(s.view_proj[0], caster).z >= 0.0f);
}

TEST_CASE("shadow cascades: a moved camera keeps the world on the same texel grid") {
    glm::vec3 to_light(0.0f, 0.0f, 1.0f); // straight down: the other up vector
    auto a = fit_shadow_cascades(camera_at({0, 0, 0}), to_light, 800.0f, 1000.0f, 1024);
    auto b = fit_shadow_cascades(camera_at({3.3f, 1.7f, 0}), to_light, 800.0f, 1000.0f, 1024);
    for (std::size_t c = 0; c < 4; ++c) {
        // The same world point is a whole number of texels apart in the two frames.
        glm::vec3 p(57.0f, -21.0f, 4.0f);
        glm::vec2 ta = glm::vec2(clip(a.view_proj[c], p)) * 512.0f;
        glm::vec2 tb = glm::vec2(clip(b.view_proj[c], p)) * 512.0f;
        glm::vec2 d = ta - tb;
        CHECK(std::abs(d.x - std::round(d.x)) < 0.01f);
        CHECK(std::abs(d.y - std::round(d.y)) < 0.01f);
    }
}

TEST_CASE("cover map: round the camera, looking down, snapped to its texels") {
    glm::vec3 cam(1000.3f, -40.7f, 25.0f);
    CoverMap m = fit_cover_map(cam, 2048);
    float texel = 2.0f * kCoverHalfSize / 2048.0f;
    CHECK(std::fmod(m.cover.x, texel) == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(m.cover.x <= cam.x - kCoverHalfSize);
    CHECK(m.cover.x > cam.x - kCoverHalfSize - texel);
    // The top snapped up to kCoverStep; the range still reaches kCoverAbove over the camera and kCoverBelow under it.
    CHECK(std::fmod(m.cover.w, kCoverStep) == doctest::Approx(0.0f));
    CHECK(m.cover.w >= cam.z + kCoverAbove);
    CHECK(m.cover.w < cam.z + kCoverAbove + kCoverStep);
    // The camera near the middle; higher things nearer (smaller depth), the range 0..1 above to below.
    glm::vec4 c = cover_clip(m, cam);
    CHECK(std::abs(c.x) < 0.01f);
    CHECK(std::abs(c.y) < 0.01f);
    CHECK(cover_clip(m, cam + glm::vec3(0, 0, 10)).z < c.z);
    CHECK(cover_clip(m, cam + glm::vec3(0, 0, kCoverAbove)).z >= 0.0f);
    CHECK(cover_clip(m, cam - glm::vec3(0, 0, kCoverBelow)).z <= 1.0f);
    CHECK(cover_clip(m, glm::vec3(cam.x, cam.y, m.cover.w)).z == doctest::Approx(0.0f));
    CHECK(cover_clip(m, glm::vec3(cam.x, cam.y, m.cover.w - kCoverAbove - kCoverBelow - kCoverStep)).z ==
          doctest::Approx(1.0f));
    // A small step up or down keeps the same map.
    CoverMap up = fit_cover_map(cam + glm::vec3(0, 0, 1.5f), 2048);
    CHECK((up.cover == m.cover && up.params == m.params));
    CHECK(fit_cover_map(cam + glm::vec3(0, 0, kCoverStep), 2048).cover.w == m.cover.w + kCoverStep);
    CHECK(cover_clip(m, cam + glm::vec3(kCoverHalfSize, 0, 0)).x == doctest::Approx(1.0f).epsilon(0.01));

    SceneLighting l;
    set_cover(l, m, true, false);
    CHECK(l.cover == m.cover);
    CHECK(l.cover_params == glm::vec4(m.params.x, 1.0f, 0.0f, 0.0f));
    set_cover(l, m, false, true);
    CHECK(l.cover_params == glm::vec4(m.params.x, 0.0f, 1.0f, 0.0f));

    // The objects' matrix places points where cover_clip does.
    glm::mat4 vp = cover_view_proj(l.cover, l.cover_params);
    for (glm::vec3 p : {cam, cam + glm::vec3(100, -50, 300), cam + glm::vec3(-200, 180, -900)}) {
        glm::vec4 a = vp * glm::vec4(p, 1.0f), b = cover_clip(m, p);
        CHECK(a.x == doctest::Approx(b.x));
        CHECK(a.y == doctest::Approx(b.y));
        CHECK(a.z == doctest::Approx(b.z));
        CHECK(a.w == doctest::Approx(1.0f));
    }
}

TEST_CASE("sky clock: the moon's phase follows the calendar") {
    nmsc::game::SkyClock clock;
    clock.set(1, 0, 0.0, 1); // midnight of the 1st: new
    CHECK(clock.moon_phase(0.0) == doctest::Approx(0.0f).epsilon(1e-4));
    clock.set(1, 0, 0.0, 15); // the 15th: full
    CHECK(clock.moon_phase(0.0) == doctest::Approx(0.5f));
    // A game day is 72 real minutes: it moves on 1 / 28 a day.
    CHECK(clock.moon_phase(72.0 * 60.0) == doctest::Approx(0.5f + 1.0f / 28.0f));
    clock.set(13, 0, 0.0, 28); // noon of the 28th, then on into the next month
    CHECK(clock.moon_phase(0.0) == doctest::Approx((27.5f) / 28.0f));
    CHECK(clock.moon_phase(72.0 * 60.0) == doctest::Approx(0.5f / 28.0f));
}

TEST_CASE("cover map: drawn again only when its view or what it shows changes") {
    const CoverMap a = fit_cover_map(glm::vec3(100.0f, 200.0f, 10.0f), 1024);
    const CoverMap b = fit_cover_map(glm::vec3(140.0f, 200.0f, 10.0f), 1024);
    CoverMapState st;
    CHECK(cover_map_stale(st, true, a, 1, false));       // never drawn
    CHECK_FALSE(cover_map_stale(st, true, a, 1, false)); // the same
    CHECK(cover_map_stale(st, true, b, 1, false));       // moved
    CHECK_FALSE(cover_map_stale(st, true, b, 1, false));
    CHECK(cover_map_stale(st, true, b, 2, false)); // a door swung, another zone, a new shadow map
    CHECK(cover_map_stale(st, true, b, 2, true));  // swaying plants in view: every frame
    CHECK(cover_map_stale(st, true, b, 2, true));
    CHECK_FALSE(cover_map_stale(st, true, b, 2, false)); // drawn last frame as it stands
    // Off for a frame: drawn afresh when it comes back.
    CHECK_FALSE(cover_map_stale(st, false, b, 2, false));
    CHECK(cover_map_stale(st, true, b, 2, false));
    // The contents tell the zone, the shadow map and the casters apart.
    CHECK(cover_contents(1, 1, 0) != cover_contents(2, 1, 0));
    CHECK(cover_contents(1, 1, 0) != cover_contents(1, 2, 0));
    CHECK(cover_contents(1, 1, 0) != cover_contents(1, 1, 5));
    CHECK(cover_contents(1, 2, 0) != cover_contents(2, 1, 0));
}
