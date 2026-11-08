#pragma once

// The graphics DLL's procedural sky (CSky) as the scene's light and fog read
// it: the weather patterns' colour maps keyed by time of day, the weather
// transition blend, sun / moon / bounce colours and the area fog. Source:
// the procedural CSky (read from EQGraphicsDX9.dll; tools/dx9/sky_reference.py prints the same
// values from the game's files).
//
//   CResourceManager::CreateSky @0x10059db0   one CSky for the whole session (CDisplay::InitWorld)
//   CSky_Init @0x1002d380                     sky.ini [default] Enabled
//   CSkyColorMap_Load @0x1002dcc0             32 x 32 A8R8G8B8 maps, the DDS bytes as they are
//   ColorSet loading 0x1002e510 / 0x1002be40  keys sorted by start, start / length = trunc(x * 65536)
//   CSkyColorMapSequence::GetMap 0x1002e010 -> 0x1002ec00 -> 0x1002eaf0
//   Blend 0x1002e8e0                          (A * w + B * (255 - w)) >> 8 per byte
//   CWeatherManager_Update @0x10033220        the weather transition blend
//   SetWeather 0x10032d80, CWeatherPattern_UpdateStorm @0x10039350
//   the colour getters CSky vtable +0x28 .. +0x40
//   CAreaFogData_Apply @0x100252b0, its time-of-day colour 0x10024da0
//
// Not modelled here: the dome, star, ring, backdrop and horizon geometry
// (render/sky_renderer draws the classic sky.s3d layers and the cloud
// patches), the satellites' textures, and the readers of colour-map rows
// 4..27 (none found).

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hailstone { class GameDir; class IniFile; }

namespace hailstone::game {

// A colour map: 32 x 32 dwords 0xAARRGGBB, row-major, row 0 the file's first
// row (the bytes after the 128-byte DDS header, unconverted and unflipped).
using ColourMap = std::array<std::uint32_t, 32 * 32>;

// The rows of column 31 the getters read.
inline constexpr int kColourRowSun = 0;
inline constexpr int kColourRowMoon = 1;
inline constexpr int kColourRowFog = 2;
inline constexpr int kColourRowAmbient = 3;
inline constexpr int kColourRowSunBounce = 28;
inline constexpr int kColourRowMoonBounce = 29;

inline std::uint32_t colour_cell(const ColourMap& m, int row, int column = 31) {
    return m[static_cast<std::size_t>(row * 32 + column)];
}

// A DDS file's colour map: the first 32 x 32 dwords after the 128-byte header.
// nullopt unless the header gives 32 x 32 (the pixel format is not checked).
std::optional<ColourMap> parse_colour_map(std::span<const std::byte> dds);

// 0x1002e8e0: each byte, alpha included, (a * w + b * (255 - w)) >> 8.
std::uint32_t blend_colour(std::uint32_t a, std::uint32_t b, std::uint32_t w);
ColourMap blend_colour_map(const ColourMap& a, const ColourMap& b, std::uint32_t w);

// (uint32)trunc(v * 65536.0f), as the DLL's fistp under a chop control word:
// a key's start or length, or the day fraction's tick t.
std::uint32_t sky_ticks(float v);

// One key of a colour set: the map index into SkyDefinitions::maps.
struct ColourKey {
    std::uint32_t start = 0;
    std::uint32_t length = 0;
    std::size_t map = 0;
};

// Inserts a key after every key whose start is not above its own (0x1002be40).
void insert_colour_key(std::vector<ColourKey>& keys, const ColourKey& key);

// What GetMap(t) gives: key K's map, the previous key's map (w == 0), or K
// blended over the previous key by w.
struct ColourPick {
    std::size_t key = 0;  // index into the keys
    std::size_t prev = 0; // the key before K (the last key when K is the first)
    std::uint32_t w = 0;
    enum class Kind : std::uint8_t { Key, Previous, Blend } kind = Kind::Key;
};
// K = the last key whose start is below t, else the last key; d = t - K.start
// (unsigned); d >= K.length gives K; otherwise w = d * 255 / K.length. The
// keys must not be empty.
ColourPick pick_colour_key(std::span<const ColourKey> keys, std::uint32_t t);

// The day fraction CDisplay_UpdateSky gives SetTimeOfDay each frame (x87 at
// the game's 24-bit precision): (hour * 60 + minute + elapsed_ms / 3000) /
// 1440, elapsed_ms counting since the sky minute last changed.
float sky_day_fraction(std::int32_t hour, std::int32_t minute, std::uint32_t elapsed_ms);
// CDisplay_UpdateSunLighting's: (hour * 60 + minute) / 1440.
float sky_day_fraction(std::int32_t hour, std::int32_t minute);
// CSkyBase::IsNight @0x100c20a0: f < 0.24652778f or f > 0.7638889f (NaN is day).
bool sky_fraction_is_night(float f);

// ---------------------------------------------------------------------------
// The ini files.

struct ColourSet {
    std::string name;
    std::vector<ColourKey> keys; // never empty
};

struct WeatherPattern {
    std::string name;
    std::optional<std::size_t> colour_set; // index into SkyDefinitions::sets
    std::string storm_weather;             // StormWeather
    std::string clear_weather;             // ClearWeather
    float transition_time = 0.0f;          // TransitionTime, seconds
};

struct SkySetting {
    std::string name;
    std::string default_weather;
};

// [AreaFog-<zone>-<name>]: colours 0..255 for dawn, noon, dusk and night,
// FogPct, FogWeight, FogDensity (0..100, default 100).
struct AreaFogData {
    std::array<std::array<std::int32_t, 3>, 4> colours{}; // dawn, noon, dusk, night
    float fog_pct = 0.0f;
    float fog_weight = 0.0f;
    float fog_density = 100.0f;
};

struct SkyDefinitions {
    bool enabled = true; // sky.ini [default] Enabled
    std::vector<ColourMap> maps;
    std::vector<ColourSet> sets;
    std::vector<WeatherPattern> patterns;
    std::vector<SkySetting> settings;
    std::unordered_map<std::string, AreaFogData> area_fogs; // lower-case "<zone>-<name>"

    // Names are matched case-insensitively (the DLL hashes them through
    // tolower and compares with _stricmp).
    const WeatherPattern* pattern(std::string_view name) const;
    const SkySetting* setting(std::string_view name) const;
    const AreaFogData* area_fog(std::string_view zone, std::string_view name) const;

    // From the two ini files; `load_map` returns a ColorMap file's map
    // (Resources/Sky/ColorMap-<File>.dds) or nullopt.
    static SkyDefinitions parse(const IniFile& sky, const IniFile& weather,
                                const std::function<std::optional<ColourMap>(const std::string& file)>& load_map);
    // Resources/Sky/sky.ini and weather.ini (eqclient.ini SkyINI / WeatherINI
    // are not read); nullptr when either is missing.
    static std::shared_ptr<const SkyDefinitions> load(const GameDir& game);
};

// The colour map of a colour set at tick t (GetMap).
ColourMap colour_set_map(const SkyDefinitions& defs, const ColourSet& set, std::uint32_t t);

// ---------------------------------------------------------------------------
// The CSky object.

class SkyState {
public:
    SkyState() = default;
    explicit SkyState(std::shared_ptr<const SkyDefinitions> defs) : defs_(std::move(defs)) {}

    bool loaded() const { return defs_ != nullptr; }
    // sky.ini [default] Enabled: without it the weather manager is not made
    // and every colour getter keeps 0.
    bool enabled() const { return defs_ && defs_->enabled; }
    const SkyDefinitions* definitions() const { return defs_.get(); }

    // vtable +0x04 SetSky(name): [SkySetting-name] when listed with a
    // DefaultWeather, else [SkySetting-default]; then SetWeather(DefaultWeather, 0).
    void set_sky(std::string_view name);
    const std::string& sky_name() const { return sky_name_; }
    // vtable +0x0c SetWeather(name, seconds) (leading spaces skipped): nothing
    // for an empty or unknown name or the current pattern; seconds < 0 takes
    // the pattern's TransitionTime.
    void set_weather(std::string_view name, float seconds);
    std::string weather_name() const;
    const WeatherPattern* current_pattern() const { return current_; }
    const WeatherPattern* previous_pattern() const { return previous_; }
    float transition_remaining() const { return remaining_; }

    // vtable +0x48 SetTimeOfDay(f); +0x44; +0x4c IsNight.
    void set_time_of_day(float f) { fraction_ = f; }
    float time_of_day() const { return fraction_; }
    bool is_night() const { return sky_fraction_is_night(fraction_); }

    // One CSky_Update while the sky is drawn: the storm switch on the current
    // precipitation intensity, then the weather manager's colour map
    // (CWeatherManager_Update). `frame_ms` is CSky +0x20 (the GetTickCount
    // delta x TimeScale); `render_fog` the render fog colour (CRender
    // +0xa9b4), the fill of a transition missing a colour set.
    void update(float frame_ms, float precipitation, std::uint32_t render_fog);

    // The colour getters (0xAARRGGBB): map[row][31] of the current map, or the
    // getter's last value (0 at first) without one.
    std::uint32_t ambient() { return read(kColourRowAmbient, ambient_); }
    std::uint32_t fog() { return read(kColourRowFog, fog_); }
    std::uint32_t sun() { return read(kColourRowSun, sun_); }
    std::uint32_t moon() { return read(kColourRowMoon, moon_); }
    std::uint32_t sun_bounce() { return read(kColourRowSunBounce, sun_bounce_); }
    std::uint32_t moon_bounce() { return read(kColourRowMoonBounce, moon_bounce_); }
    const std::optional<ColourMap>& current_map() const { return map_; }

    // The area fog (+0x5c .. +0x70, kept by the weather manager).
    bool area_fog() const { return area_fog_; }
    void set_area_fog(bool on) { area_fog_ = on; }
    std::uint32_t area_fog_colour() const { return area_fog_colour_; }
    void set_area_fog_colour(std::uint32_t c) { area_fog_colour_ = c; }
    float area_fog_density() const { return area_fog_density_; }
    void set_area_fog_density(float d) { area_fog_density_ = d; }

private:
    std::uint32_t read(int row, std::uint32_t& last) {
        if (map_) last = colour_cell(*map_, row);
        return last;
    }
    std::optional<ColourMap> set_map(const WeatherPattern* p) const;

    std::shared_ptr<const SkyDefinitions> defs_;
    std::string sky_name_;
    const WeatherPattern* current_ = nullptr;  // +0x388
    const WeatherPattern* previous_ = nullptr; // +0x38c
    float total_ = 0.0f;                       // +0x390, ms
    float remaining_ = 0.0f;                   // +0x394, ms
    std::int32_t last_k_ = -1;                 // +0x13f1 (its first value is not known)
    ColourMap buffer_{};                       // +0x398
    float fraction_ = 0.0f;                    // +4
    std::optional<ColourMap> map_;             // +0x13c4
    std::uint32_t ambient_ = 0, fog_ = 0, sun_ = 0, moon_ = 0, sun_bounce_ = 0, moon_bounce_ = 0;
    bool area_fog_ = false;
    std::uint32_t area_fog_colour_ = 0;
    float area_fog_density_ = 0.0f;
};

// ---------------------------------------------------------------------------
// Area fog (AFG_<name> region boxes).

// 0x10024da0: the area fog colour for hour:minute: 6..11 dawn to noon over
// 6:00-12:00, 12..15 noon to dusk over 12:00-16:00, 16..17 dusk to night over
// 16:00-18:00, otherwise night to dawn with h' = h - 18 (h >= 18) or h + 5
// and the fraction (h' 60 + m) / 660; each channel from + round((to - from) x).
std::array<std::int32_t, 3> area_fog_time_colour(const AreaFogData& fog, std::int32_t hour, std::int32_t minute);

struct AreaFogResult {
    std::uint32_t colour = 0; // 0xffRRGGBB
    float density = 0.0f;
};
// CAreaFogData_Apply @0x100252b0: d = the xy distance from the box centre;
// inside R the weight is FogWeight x 0.01 up to FogPct x 0.01 x R and falls
// linearly to 0 at R; colour = zone + round((c - zone) t) per channel,
// density = zone + (FogDensity x 0.01 - zone) t. Outside R the zone's.
AreaFogResult area_fog_blend(const AreaFogData& fog, float radius, float distance, std::uint32_t zone_colour,
                             float zone_density, std::int32_t hour, std::int32_t minute);

} // namespace hailstone::game
