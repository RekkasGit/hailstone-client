#include "game/sky_colours.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>

#include "core/game_dir.h"
#include "core/ini_file.h"
#include "core/log.h"
#include "game/mechanics/fpu.h"

namespace nmsc::game {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool iequal(std::string_view a, std::string_view b) { return lower(a) == lower(b); }

std::string trimmed(std::string_view s) {
    const char* ws = " \t\r\n";
    std::size_t b = s.find_first_not_of(ws);
    if (b == std::string_view::npos) return {};
    std::size_t e = s.find_last_not_of(ws);
    return std::string(s.substr(b, e - b + 1));
}

std::optional<std::string> text(const IniFile& ini, std::string_view section, std::string_view key) {
    auto v = ini.get(section, key);
    if (!v) return std::nullopt;
    return trimmed(*v);
}

// sscanf("%f") into a float; `fallback` when the key is missing or does not parse.
float number(const IniFile& ini, std::string_view section, std::string_view key, float fallback) {
    auto v = text(ini, section, key);
    if (!v || v->empty()) return fallback;
    char* end = nullptr;
    float f = std::strtof(v->c_str(), &end);
    return end == v->c_str() ? fallback : f;
}

std::int32_t integer(const IniFile& ini, std::string_view section, std::string_view key, std::int32_t fallback) {
    auto v = text(ini, section, key);
    if (!v || v->empty()) return fallback;
    char* end = nullptr;
    long n = std::strtol(v->c_str(), &end, 10);
    return end == v->c_str() ? fallback : static_cast<std::int32_t>(n);
}

constexpr float kNightStart = 0.24652778f; // CSkyBase::IsNight's constants
constexpr float kNightEnd = 0.7638889f;

} // namespace

std::optional<ColourMap> parse_colour_map(std::span<const std::byte> dds) {
    if (dds.size() < 128 + 32 * 32 * 4) return std::nullopt;
    auto u32 = [&](std::size_t at) {
        return static_cast<std::uint32_t>(dds[at]) | static_cast<std::uint32_t>(dds[at + 1]) << 8 |
               static_cast<std::uint32_t>(dds[at + 2]) << 16 | static_cast<std::uint32_t>(dds[at + 3]) << 24;
    };
    if (u32(12) != 32 || u32(16) != 32) return std::nullopt; // dwHeight, dwWidth
    ColourMap m{};
    for (std::size_t i = 0; i < m.size(); ++i) m[i] = u32(128 + i * 4);
    return m;
}

std::uint32_t blend_colour(std::uint32_t a, std::uint32_t b, std::uint32_t w) {
    std::uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        std::uint32_t x = a >> shift & 0xff, y = b >> shift & 0xff;
        out |= ((x * w + y * (255 - w)) >> 8 & 0xff) << shift;
    }
    return out;
}

ColourMap blend_colour_map(const ColourMap& a, const ColourMap& b, std::uint32_t w) {
    ColourMap out{};
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = blend_colour(a[i], b[i], w);
    return out;
}

std::uint32_t sky_ticks(float v) {
    float x = v * 65536.0f; // exact: a power of two
    if (!(x > -2147483649.0f && x < 2147483648.0f)) return 0x80000000u; // fistp's integer indefinite
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(std::trunc(x)));
}

void insert_colour_key(std::vector<ColourKey>& keys, const ColourKey& key) {
    auto at = std::find_if(keys.begin(), keys.end(), [&](const ColourKey& k) { return key.start < k.start; });
    keys.insert(at, key);
}

ColourPick pick_colour_key(std::span<const ColourKey> keys, std::uint32_t t) {
    ColourPick p;
    if (keys.size() <= 1) return p;
    std::size_t k = keys.size() - 1; // times before the first key use the last one
    for (std::size_t i = 0; i < keys.size(); ++i)
        if (keys[i].start < t) k = i;
    p.key = k;
    p.prev = k > 0 ? k - 1 : keys.size() - 1;
    std::uint32_t d = t - keys[k].start; // wraps for the last key before the first
    if (d >= keys[k].length) return p;
    p.w = (d * 255u) / keys[k].length; // uint32 arithmetic, as the DLL's
    p.kind = p.w == 0 ? ColourPick::Kind::Previous : ColourPick::Kind::Blend;
    return p;
}

float sky_day_fraction(std::int32_t hour, std::int32_t minute, std::uint32_t elapsed_ms) {
    const mechanics::Fpu fpu; // 24-bit, as in game
    double a = fpu.add(fpu.mul(static_cast<double>(hour), 60.0), static_cast<double>(minute));
    // fild elapsed (+2^32 when negative as an int32) / fmul 1/3000 / faddp / fmul 1/1440 / fstp.
    double e = static_cast<double>(static_cast<std::int32_t>(elapsed_ms));
    if (static_cast<std::int32_t>(elapsed_ms) < 0) e = fpu.add(e, 4294967296.0);
    e = fpu.mul(e, static_cast<double>(1.0f / 3000.0f));
    a = fpu.add(a, e);
    return static_cast<float>(fpu.mul(a, static_cast<double>(1.0f / 1440.0f)));
}

float sky_day_fraction(std::int32_t hour, std::int32_t minute) {
    const mechanics::Fpu fpu;
    double a = fpu.add(fpu.mul(static_cast<double>(hour), 60.0), static_cast<double>(minute));
    return static_cast<float>(fpu.mul(a, static_cast<double>(1.0f / 1440.0f)));
}

bool sky_fraction_is_night(float f) { return f < kNightStart || f > kNightEnd; }

// ---------------------------------------------------------------------------

const WeatherPattern* SkyDefinitions::pattern(std::string_view name) const {
    for (const auto& p : patterns)
        if (iequal(p.name, name)) return &p;
    return nullptr;
}

const SkySetting* SkyDefinitions::setting(std::string_view name) const {
    for (const auto& s : settings)
        if (iequal(s.name, name)) return &s;
    return nullptr;
}

const AreaFogData* SkyDefinitions::area_fog(std::string_view zone, std::string_view name) const {
    auto it = area_fogs.find(lower(zone) + "-" + lower(name));
    return it == area_fogs.end() ? nullptr : &it->second;
}

SkyDefinitions SkyDefinitions::parse(const IniFile& sky, const IniFile& weather,
                                     const std::function<std::optional<ColourMap>(const std::string&)>& load_map) {
    SkyDefinitions d;
    d.enabled = integer(sky, "default", "Enabled", 1) != 0;

    // [ColorMaps] -> [ColorMap-<name>] File: only listed entries load.
    std::unordered_map<std::string, std::size_t> map_index;
    for (const std::string& name : weather.keys("ColorMaps")) {
        auto file = text(weather, "ColorMap-" + name, "File");
        if (!file || file->empty() || map_index.count(lower(name))) continue;
        auto m = load_map(*file);
        if (!m) {
            log::debug("sky: ColorMap {} ({}) not loaded", name, *file);
            continue;
        }
        map_index.emplace(lower(name), d.maps.size());
        d.maps.push_back(*m);
    }

    // [ColorSets] -> [ColorSet-<name>]: ColorMap<n>, Time<n>, Transition<n> until ColorMap<n> is missing; a map
    // that did not load fails the whole set, and so does a set without keys.
    std::unordered_map<std::string, std::size_t> set_index;
    for (const std::string& name : weather.keys("ColorSets")) {
        if (set_index.count(lower(name))) continue;
        std::string section = "ColorSet-" + name;
        ColourSet set;
        set.name = name;
        bool ok = true;
        for (int n = 0;; ++n) {
            auto map_name = text(weather, section, "ColorMap" + std::to_string(n));
            if (!map_name) break;
            auto it = map_index.find(lower(*map_name));
            if (it == map_index.end()) {
                log::debug("sky: ColorSet {} is unable to find ColorMap {}", name, *map_name);
                ok = false;
                break;
            }
            ColourKey key;
            key.start = sky_ticks(number(weather, section, "Time" + std::to_string(n), 0.0f));
            key.length = sky_ticks(number(weather, section, "Transition" + std::to_string(n), 0.0f));
            key.map = it->second;
            insert_colour_key(set.keys, key);
        }
        if (!ok || set.keys.empty()) continue;
        set_index.emplace(lower(name), d.sets.size());
        d.sets.push_back(std::move(set));
    }

    // [WeatherPatterns] -> [WeatherPattern-<name>].
    for (const std::string& name : weather.keys("WeatherPatterns")) {
        if (d.pattern(name)) continue;
        std::string section = "WeatherPattern-" + name;
        if (!weather.get(section, "ColorSet") && !weather.get(section, "TransitionTime")) continue;
        WeatherPattern p;
        p.name = name;
        if (auto cs = text(weather, section, "ColorSet")) {
            auto it = set_index.find(lower(*cs));
            if (it != set_index.end()) p.colour_set = it->second;
        }
        p.storm_weather = text(weather, section, "StormWeather").value_or("");
        p.clear_weather = text(weather, section, "ClearWeather").value_or("");
        p.transition_time = number(weather, section, "TransitionTime", 0.0f);
        d.patterns.push_back(std::move(p));
    }

    // [SkySettings] -> [SkySetting-<name>]; a section without DefaultWeather is dropped (0x100c3d09).
    for (const std::string& name : sky.keys("SkySettings")) {
        if (d.setting(name)) continue;
        auto w = text(sky, "SkySetting-" + name, "DefaultWeather");
        if (!w || w->empty()) continue;
        d.settings.push_back({name, *w});
    }

    // [AreaFogSettings] -> [AreaFog-<zone>-<name>].
    for (const std::string& name : weather.keys("AreaFogSettings")) {
        std::string section = "AreaFog-" + name;
        AreaFogData f;
        const char* const times[4] = {"Dawn", "Noon", "Dusk", "Night"};
        const char* const channels[3] = {"R", "G", "B"};
        for (int t = 0; t < 4; ++t)
            for (int c = 0; c < 3; ++c)
                f.colours[static_cast<std::size_t>(t)][static_cast<std::size_t>(c)] =
                    integer(weather, section, std::string(times[t]) + "FogColor" + channels[c], 0);
        f.fog_pct = number(weather, section, "FogPct", 0.0f);
        f.fog_weight = number(weather, section, "FogWeight", 0.0f);
        f.fog_density = number(weather, section, "FogDensity", 100.0f);
        d.area_fogs.emplace(lower(name), f);
    }
    return d;
}

std::shared_ptr<const SkyDefinitions> SkyDefinitions::load(const GameDir& game) {
    IniFile sky, weather;
    auto sky_path = game.find("Resources/Sky/sky.ini");
    auto weather_path = game.find("Resources/Sky/weather.ini");
    if (!sky_path || !weather_path || !sky.load(*sky_path) || !weather.load(*weather_path)) return nullptr;
    auto defs = std::make_shared<SkyDefinitions>(parse(sky, weather, [&](const std::string& file) -> std::optional<ColourMap> {
        auto bytes = game.read("Resources/Sky/ColorMap-" + file + ".dds");
        if (!bytes) return std::nullopt;
        auto m = parse_colour_map(*bytes);
        if (!m) log::warn("sky: ColorMap-{}.dds is not 32x32", file);
        return m;
    }));
    log::info("sky: {} colour maps, {} colour sets, {} weather patterns, {} sky settings, {} area fogs", defs->maps.size(),
              defs->sets.size(), defs->patterns.size(), defs->settings.size(), defs->area_fogs.size());
    return defs;
}

ColourMap colour_set_map(const SkyDefinitions& defs, const ColourSet& set, std::uint32_t t) {
    ColourPick p = pick_colour_key(set.keys, t);
    const ColourMap& k = defs.maps[set.keys[p.key].map];
    switch (p.kind) {
    case ColourPick::Kind::Key: return k;
    case ColourPick::Kind::Previous: return defs.maps[set.keys[p.prev].map];
    case ColourPick::Kind::Blend: break;
    }
    return blend_colour_map(k, defs.maps[set.keys[p.prev].map], p.w);
}

// ---------------------------------------------------------------------------

void SkyState::set_sky(std::string_view name) {
    if (!defs_) return;
    const SkySetting* s = defs_->setting(name);
    if (!s) s = defs_->setting("default");
    sky_name_ = std::string(name);
    if (!s) return;
    set_weather(s->default_weather, 0.0f); // +0x50: SetWeather(DefaultWeather, 0)
}

void SkyState::set_weather(std::string_view name, float seconds) {
    if (!enabled()) return; // no weather manager
    while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
    if (name.empty()) return;
    const WeatherPattern* p = defs_->pattern(name);
    if (!p || p == current_) return;
    if (seconds < 0.0f) seconds = p->transition_time;
    total_ = remaining_ = seconds * 1000.0f;
    previous_ = current_;
    current_ = p;
}

std::string SkyState::weather_name() const { return current_ ? current_->name : std::string(); }

std::optional<ColourMap> SkyState::set_map(const WeatherPattern* p) const {
    if (!p || !p->colour_set) return std::nullopt;
    return colour_set_map(*defs_, defs_->sets[*p->colour_set], sky_ticks(fraction_));
}

void SkyState::update(float frame_ms, float precipitation, std::uint32_t render_fog) {
    if (!enabled()) return;
    // CWeatherPattern_UpdateStorm: the precipitation manager's intensity switches to the storm pattern and back.
    if (current_) {
        if (precipitation > 0.0f && !current_->storm_weather.empty())
            set_weather(current_->storm_weather, current_->transition_time);
        else if (precipitation == 0.0f && !current_->clear_weather.empty())
            set_weather(current_->clear_weather, current_->transition_time);
    }
    // CWeatherManager_Update.
    std::optional<ColourMap> map;
    if (previous_ && remaining_ > 0.0f) {
        remaining_ -= frame_ms;
        if (remaining_ > 0.0f) {
            float r = remaining_ / total_;
            float scaled = r * 255.0f;
            // fistp with chop into a byte (r is in (0, 1] here, so k is 0..255).
            std::int32_t k = std::isfinite(scaled) ? static_cast<std::uint8_t>(static_cast<std::int32_t>(std::trunc(scaled))) : 0;
            if (k != last_k_) {
                auto a = set_map(previous_);
                auto b = set_map(current_);
                if (a && b) {
                    buffer_ = blend_colour_map(*a, *b, static_cast<std::uint32_t>(k));
                } else {
                    buffer_.fill(render_fog | 0xff000000u);
                }
                last_k_ = k;
            }
            map = buffer_;
        }
    }
    if (!map) map = set_map(current_);
    map_ = map;
}

// ---------------------------------------------------------------------------

std::array<std::int32_t, 3> area_fog_time_colour(const AreaFogData& fog, std::int32_t hour, std::int32_t minute) {
    int from = 3, to = 0;
    float x = 0.0f;
    if (hour >= 6 && hour < 12) {
        from = 0, to = 1;
        x = static_cast<float>((hour - 6) * 60 + minute) / 360.0f;
    } else if (hour >= 12 && hour < 16) {
        from = 1, to = 2;
        x = static_cast<float>((hour - 12) * 60 + minute) / 240.0f;
    } else if (hour >= 16 && hour < 18) {
        from = 2, to = 3;
        x = static_cast<float>((hour - 16) * 60 + minute) / 120.0f;
    } else {
        std::int32_t h = hour >= 18 ? hour - 18 : hour + 5;
        x = static_cast<float>(h * 60 + minute) / 660.0f;
    }
    std::array<std::int32_t, 3> out{};
    for (std::size_t c = 0; c < 3; ++c) {
        std::int32_t a = fog.colours[static_cast<std::size_t>(from)][c];
        std::int32_t b = fog.colours[static_cast<std::size_t>(to)][c];
        out[c] = a + static_cast<std::int32_t>(std::nearbyint(static_cast<float>(b - a) * x));
    }
    return out;
}

AreaFogResult area_fog_blend(const AreaFogData& fog, float radius, float distance, std::uint32_t zone_colour,
                             float zone_density, std::int32_t hour, std::int32_t minute) {
    AreaFogResult r{zone_colour, zone_density};
    if (!(distance * distance < radius * radius)) return r;
    float inner = fog.fog_pct * 0.01f * radius;
    float weight = fog.fog_weight * 0.01f;
    float t = distance * distance <= inner * inner ? weight : weight * (distance - radius) / (inner - radius);
    auto c = area_fog_time_colour(fog, hour, minute);
    std::uint32_t colour = 0xff000000u;
    for (int i = 0; i < 3; ++i) {
        int shift = 16 - 8 * i;
        auto zone = static_cast<std::int32_t>(zone_colour >> shift & 0xff);
        auto v = zone + static_cast<std::int32_t>(std::nearbyint(static_cast<float>(c[static_cast<std::size_t>(i)] - zone) * t));
        colour |= (static_cast<std::uint32_t>(v) & 0xff) << shift;
    }
    r.colour = colour;
    r.density = zone_density + (fog.fog_density * 0.01f - zone_density) * t;
    return r;
}

} // namespace nmsc::game
