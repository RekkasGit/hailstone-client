// The CSky colour maps (game/sky_colours.h), the weather's per-frame side (game/weather_state.h) and the scene light's sky
// colours. The expected colours with the game files are tools/dx9/sky_reference.py's (rof2decomp), run on the same
// install with `python3 -I tools/dx9/sky_reference.py --zone <zone> --times ...`.
#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <vector>
#include <cstdint>
#include <tuple>

#include "core/ini_file.h"
#include "game/sky_colours.h"
#include "game/weather_state.h"
#include "game_files.h"

using namespace hailstone::game;

namespace {

using Rgb = std::tuple<int, int, int>;
Rgb rgb(std::uint32_t c) { return {static_cast<int>(c >> 16 & 0xff), static_cast<int>(c >> 8 & 0xff), static_cast<int>(c & 0xff)}; }

ColourMap filled(std::uint32_t c) {
    ColourMap m;
    m.fill(c);
    return m;
}

} // namespace

TEST_CASE("sky colour maps: blend, keys and the day fraction") {
    // >> 8 divides by 256: at w = 254 a byte of 255 gives 254; all four bytes blend.
    CHECK(blend_colour(0xffffffffu, 0xffffffffu, 254) == 0xfefefefeu);
    CHECK(blend_colour(0x00000000u, 0xffffffffu, 0) == 0xfefefefeu);
    CHECK(blend_colour(0xffffffffu, 0x00000000u, 254) == 0xfdfdfdfdu);
    CHECK(blend_colour(0x80402010u, 0x10204080u, 128) == 0x472f2f47u);

    CHECK(sky_ticks(0.234985f) == 15399u);
    CHECK(sky_ticks(0.034988f) == 2292u);
    CHECK(sky_ticks(0.5f) == 32768u);

    std::vector<ColourKey> keys;
    insert_colour_key(keys, {48495, 1966, 3});
    insert_colour_key(keys, {15399, 2292, 0});
    insert_colour_key(keys, {45875, 1309, 2});
    insert_colour_key(keys, {18350, 1309, 1});
    insert_colour_key(keys, {18350, 5, 9}); // after the equal one
    REQUIRE(keys.size() == 5);
    CHECK(keys[0].map == 0);
    CHECK(keys[1].map == 1);
    CHECK(keys[2].map == 9);
    keys.erase(keys.begin() + 2);
    // Before the first key: the last key (d wraps, so its map whole).
    auto p = pick_colour_key(keys, 0);
    CHECK(p.key == 3);
    CHECK(p.kind == ColourPick::Kind::Key);
    // A start equal to t is not below it: the key before.
    p = pick_colour_key(keys, 15399);
    CHECK(p.key == 3);
    p = pick_colour_key(keys, 15400); // d = 1: w = 255 / 2292 = 0 -> the previous map
    CHECK(p.key == 0);
    CHECK(p.prev == 3);
    CHECK(p.kind == ColourPick::Kind::Previous);
    p = pick_colour_key(keys, 16839); // 6:10
    CHECK(p.kind == ColourPick::Kind::Blend);
    CHECK(p.w == 160u);
    p = pick_colour_key(keys, 32768);
    CHECK(p.key == 1);
    CHECK(p.kind == ColourPick::Kind::Key);

    CHECK(sky_day_fraction(12, 0) == 0.5f);
    CHECK(sky_day_fraction(12, 0, 0) == 0.5f);
    CHECK(sky_day_fraction(12, 0, 1500) == doctest::Approx(720.5 / 1440.0));
    CHECK(sky_fraction_is_night(sky_day_fraction(5, 54)));
    CHECK_FALSE(sky_fraction_is_night(sky_day_fraction(5, 55)));
    CHECK_FALSE(sky_fraction_is_night(sky_day_fraction(18, 20))); // equal to the bound
    CHECK(sky_fraction_is_night(sky_day_fraction(18, 21)));
    CHECK(sky_fraction_is_night(sky_day_fraction(24, 0)));
}

TEST_CASE("sky state: ini selection, weather transitions, storms and the getters") {
    hailstone::IniFile sky, weather;
    sky.parse("[default]\nEnabled=1\n[SkySettings]\ndefault=\nqeynos=\nnoweather=\n[SkySetting-qeynos]\nDefaultWeather=Clear\n"
              "[SkySetting-noweather]\nDefaultBackdrop=x\n[SkySetting-default]\nDefaultWeather=Other\n");
    weather.parse("[ColorMaps]\nA=\nB=\nMissing=\n[ColorMap-A]\nFile=a\n[ColorMap-B]\nFile=b\n[ColorMap-Missing]\nFile=none\n"
                  "[ColorSets]\nClear=\nRain=\nBroken=\n[ColorSet-Clear]\nColorMap0=A\nTime0=0.25\nTransition0=0\n"
                  "[ColorSet-Rain]\nColorMap0=B\nTime0=0.25\n[ColorSet-Broken]\nColorMap0=A\nColorMap1=Missing\n"
                  "[WeatherPatterns]\nClear=\nRain=\nOther=\nNoSet=\n"
                  "[WeatherPattern-Clear]\nColorSet=Clear\nStormWeather=Rain\nClearWeather=\nTransitionTime=10\n"
                  "[WeatherPattern-Rain]\nColorSet=Rain\nStormWeather=\nClearWeather=Clear\nTransitionTime=10\n"
                  "[WeatherPattern-Other]\nColorSet=Broken\nTransitionTime=2\n[WeatherPattern-NoSet]\nTransitionTime=1\n");
    auto defs = std::make_shared<SkyDefinitions>(SkyDefinitions::parse(sky, weather, [](const std::string& f) -> std::optional<ColourMap> {
        if (f == "a") return filled(0xff102030u);
        if (f == "b") return filled(0xff908070u);
        return std::nullopt;
    }));
    CHECK(defs->maps.size() == 2);
    CHECK(defs->sets.size() == 2); // Broken fails as a whole
    REQUIRE(defs->setting("QEYNOS"));
    CHECK_FALSE(defs->setting("noweather")); // dropped: no DefaultWeather
    REQUIRE(defs->pattern("other"));
    CHECK_FALSE(defs->pattern("other")->colour_set);

    SkyState s(defs);
    CHECK(s.ambient() == 0); // no map yet
    s.set_sky("qeynos2"); // not listed: [SkySetting-default] -> Other (no colour set)
    CHECK(s.weather_name() == "Other");
    s.update(16.0f, 0.0f, 0);
    CHECK_FALSE(s.current_map());
    s.set_sky("QEYNOS");
    CHECK(s.weather_name() == "Clear");
    s.set_time_of_day(0.5f);
    s.update(16.0f, 0.0f, 0x112233);
    CHECK(s.ambient() == 0xff102030u); // SetSky's SetWeather(DefaultWeather, 0) is immediate

    // A storm: Rain over TransitionTime 10 s, blended Clear * k + Rain * (255 - k), k = trunc(remaining / total * 255).
    s.update(5000.0f, 0.05f, 0);
    CHECK(s.weather_name() == "Rain");
    CHECK(s.transition_remaining() == 5000.0f);
    CHECK(s.fog() == blend_colour(0xff102030u, 0xff908070u, 127));
    s.update(6000.0f, 0.05f, 0);
    CHECK(s.fog() == 0xff908070u);
    // The precipitation back at 0: ClearWeather.
    s.update(16.0f, 0.0f, 0);
    CHECK(s.weather_name() == "Clear");
    // Unknown names, the current one and empty names change nothing.
    s.set_weather("nothing", 1.0f);
    s.set_weather("  Clear", 1.0f);
    s.set_weather("", 1.0f);
    CHECK(s.weather_name() == "Clear");
    // To a pattern without a colour set: the transition fills with the render fog colour.
    s.set_weather("NoSet", -1.0f); // TransitionTime 1 s
    s.update(500.0f, 0.0f, 0x00445566u);
    CHECK(s.ambient() == 0xff445566u);
    s.update(600.0f, 0.0f, 0);
    CHECK_FALSE(s.current_map());
    CHECK(s.ambient() == 0xff445566u); // the getter keeps its last value
}

TEST_CASE("sky colours from the game's sky.ini and weather.ini (sky_reference.py)") {
    const hailstone::GameDir* game = hailstone::test::game_dir();
    if (!game) {
        MESSAGE("EQ_GAME_DIR not set; skipped");
        return;
    }
    auto defs = SkyDefinitions::load(*game);
    REQUIRE(defs);
    SkyState s(defs);
    s.set_sky("qeynos2");
    CHECK(s.weather_name() == "DefaultClear");
    struct Row {
        int h, m;
        Rgb ambient, fog, sun, moon, sun_bounce, moon_bounce;
        bool night;
    };
    const Row rows[] = {
        {12, 0, {219, 217, 217}, {143, 154, 184}, {255, 255, 255}, {0, 0, 0}, {189, 188, 188}, {189, 188, 188}, false},
        {18, 0, {61, 41, 71}, {60, 49, 83}, {169, 102, 23}, {4, 28, 76}, {44, 32, 51}, {56, 44, 66}, false},
        {19, 0, {0, 0, 0}, {2, 5, 12}, {0, 0, 0}, {13, 86, 231}, {4, 17, 42}, {39, 55, 87}, true},
        {24, 0, {0, 0, 0}, {2, 5, 12}, {0, 0, 0}, {13, 86, 231}, {4, 17, 42}, {39, 55, 87}, true},
        {6, 10, {40, 25, 35}, {85, 59, 75}, {158, 108, 50}, {47, 80, 158}, {42, 31, 50}, {55, 45, 67}, false},
        {5, 50, {14, 9, 12}, {32, 24, 34}, {57, 39, 18}, {25, 83, 204}, {17, 22, 45}, {44, 51, 79}, true},
        {17, 50, {82, 55, 95}, {80, 64, 108}, {228, 137, 32}, {1, 8, 23}, {58, 37, 54}, {62, 41, 58}, false},
    };
    for (const Row& r : rows) {
        CAPTURE(r.h);
        CAPTURE(r.m);
        s.set_time_of_day(sky_day_fraction(r.h, r.m));
        s.update(16.0f, 0.0f, 0);
        CHECK(rgb(s.ambient()) == r.ambient);
        CHECK(rgb(s.fog()) == r.fog);
        CHECK(rgb(s.sun()) == r.sun);
        CHECK(rgb(s.moon()) == r.moon);
        CHECK(rgb(s.sun_bounce()) == r.sun_bounce);
        CHECK(rgb(s.moon_bounce()) == r.moon_bounce);
        CHECK(s.is_night() == r.night);
    }

    // Rain coming in: DefaultClear -> DefaultRain over 10 s, half way (sky_reference.py --weather DefaultRain
    // --from-pattern DefaultClear --remaining 0.5).
    s.set_time_of_day(sky_day_fraction(12, 0));
    s.update(16.0f, 0.05f, 0); // the storm switch
    CHECK(s.weather_name() == "DefaultRain");
    s.update(5000.0f - 16.0f, 0.05f, 0);
    CHECK(rgb(s.ambient()) == Rgb{132, 143, 152});
    CHECK(rgb(s.fog()) == Rgb{119, 128, 151});
    CHECK(rgb(s.sun()) == Rgb{205, 208, 210});

    // An EQG zone's own setting: arthicrex -> illsalin-0, one key.
    s.set_sky("arthicrex");
    CHECK(s.weather_name() == "illsalin-0");
    s.update(16.0f, 0.0f, 0);
    CHECK(rgb(s.ambient()) == Rgb{0, 75, 83});
    CHECK(rgb(s.sun()) == Rgb{215, 215, 215});
    // NMS's weather.ini has no area fog.
    CHECK(defs->area_fogs.empty());
}

TEST_CASE("area fog blend") {
    AreaFogData f;
    f.colours = {{{200, 0, 0}, {0, 200, 0}, {0, 0, 200}, {100, 100, 100}}};
    f.fog_pct = 50.0f;
    f.fog_weight = 80.0f;
    f.fog_density = 50.0f;
    // Noon: from noon toward dusk by (h - 12) 60 + m over 240.
    CHECK(area_fog_time_colour(f, 12, 0) == std::array<std::int32_t, 3>{0, 200, 0});
    CHECK(area_fog_time_colour(f, 14, 0) == std::array<std::int32_t, 3>{0, 100, 100});
    CHECK(area_fog_time_colour(f, 18, 0) == std::array<std::int32_t, 3>{100, 100, 100});
    CHECK(area_fog_time_colour(f, 6, 0) == std::array<std::int32_t, 3>{200, 0, 0});
    // Inside FogPct x R the full weight 0.8; then linear to 0 at R; outside the zone's.
    auto r = area_fog_blend(f, 100.0f, 10.0f, 0xff000000u, 0.2f, 12, 0);
    CHECK(r.colour == 0xff00a000u); // 200 * 0.8
    CHECK(r.density == doctest::Approx(0.2f + (0.5f - 0.2f) * 0.8f));
    r = area_fog_blend(f, 100.0f, 75.0f, 0xff000000u, 0.2f, 12, 0);
    CHECK(r.colour == 0xff005000u); // weight 0.4
    r = area_fog_blend(f, 100.0f, 100.0f, 0xff123456u, 0.2f, 12, 0);
    CHECK(r.colour == 0xff123456u);
    CHECK(r.density == 0.2f);
}

namespace {

struct Fx : WeatherEffects {
    float near = 300.0f, far = 2000.0f;
    std::vector<std::uint32_t> chats;
    std::vector<std::int32_t> waves;
    bool raining_sound = false;
    int sky_updates = 0;
    std::int32_t rand_value = 0;
    void set_fog(std::uint32_t n, std::uint32_t f) override {
        near = std::bit_cast<float>(n);
        far = std::bit_cast<float>(f);
    }
    float fog_near() override { return near; }
    float fog_far() override { return far; }
    void update_sky_weather_and_fog() override { ++sky_updates; }
    void play_wave(std::int32_t id) override { waves.push_back(id); }
    void emitter_raining(bool on) override { raining_sound = on; }
    void emitter_wind(bool) override {}
    void chat(std::uint32_t id) override { chats.push_back(id); }
    std::int32_t zone_id() override { return 1; }
    std::int32_t random() override { return 0; }
    std::int32_t rand() override { return rand_value; }
};

} // namespace

TEST_CASE("weather: rain fades the fog in, lightning strikes, and it stops") {
    WeatherState w;
    WeatherDisplay d;
    Precipitation p;
    Fx fx;
    const mechanics::Fpu fpu;
    d.frame_time = 1000;
    weather_handle_message(w, 0, 10);
    StartEffectEnv env;
    env.has_manager = false;
    env.now = 1000;
    weather_start_effect(fpu, w, d, p, env, w.pending_kind, w.pending_intensity);
    CHECK(d.raining == 1);
    CHECK(p.target() == static_cast<float>(10.0 * static_cast<double>(0.01f)));
    p.update(1000);
    CHECK(p.intensity() == 0.0f);
    p.update(1200); // 0.25 per second
    CHECK(p.intensity() == doctest::Approx(0.05f));
    p.update(2000);
    CHECK(p.intensity() == p.target());

    // 64 fade steps close the fog to 100..1000.
    for (int i = 0; i < 64; ++i) {
        weather_start(w, d, fx);
        d.frame_time += 16;
    }
    CHECK(w.fade == 0x100);
    CHECK(fx.near == 100.0f);
    CHECK(fx.far == 1000.0f);
    weather_start(w, d, fx); // the end of the fade: the rain message and sound
    CHECK(fx.chats == std::vector<std::uint32_t>{0x21d});
    CHECK(fx.raining_sound);
    CHECK(w.active == 0);
    // Lightning: at the strike time, thunder 0x8f (Random() % 100 + 1 = 1), then 10..20 s on (rand() 0 -> 10 s).
    weather_start(w, d, fx);
    CHECK(w.lightning == 1);
    CHECK(fx.waves == std::vector<std::int32_t>{0x8f});
    CHECK(w.thunder == 0x8f); // rand() % 100 < 30: a second strike within a second
    std::uint32_t strike = w.next_strike;
    CHECK(strike == d.frame_time);
    w.lightning = 0;
    weather_start(w, d, fx);
    CHECK(fx.waves == std::vector<std::int32_t>{0x8f, 0x90}); // the other thunder
    CHECK(w.next_strike == d.frame_time + 10000);

    // Intensity 0 fades it out: the fog steps back by the offsets, then the raining flag drops.
    weather_handle_message(w, 0xff, 0);
    weather_start_effect(fpu, w, d, p, env, w.pending_kind, w.pending_intensity);
    CHECK(w.active == 1);
    CHECK(w.on == 0);
    CHECK(w.fade == 0);
    for (int i = 0; i < 43; ++i) {
        weather_start(w, d, fx);
        d.frame_time += 16;
    }
    CHECK(w.fade == 258);
    CHECK(fx.near == doctest::Approx(100.0f + 43.0f * 200.0f / 64.0f));
    weather_start(w, d, fx);
    CHECK(d.raining == 0);
    CHECK(w.kind == 0xff);
    CHECK_FALSE(fx.raining_sound);
}
