#pragma once

// The cloud definitions of the graphics DLL's procedural sky (CSky):
// Resources/Sky/sky.ini [SkySetting-<zone>] names a DefaultWeather, whose
// weather.ini [WeatherPattern-<name>] lists Cloud0..CloudN, each a
// [Cloud-<name>] section (read by CCloudDefinition_Load @0x100278b0;
// rof2decomp notes/eqgraphicsdx9.md, Sky). Only the clouds are used here; the
// colour maps, satellites and rings of CSky are not decoded.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nmsc { class GameDir; class IniFile; }

namespace nmsc::assets {

// The most patches one cloud definition makes (DesiredCount is capped to it).
inline constexpr std::int32_t kMaxCloudPatches = 256;

struct CloudDefinition {
    std::string name;    // the section after "Cloud-"
    std::string texture; // Resources/Sky/Cloud-<Texture>.dds
    std::string color_set;
    std::int32_t desired_count = 0;
    float spawn_interval_min = 0.0f, spawn_interval_max = 0.0f; // seconds
    float size_min = 0.0f, size_max = 0.0f;
    float offset_min = 0.0f, offset_max = 0.0f;
    float velocity_min = 0.0f, velocity_max = 0.0f; // radians per ms of sky time
    float spin_min = 0.0f, spin_max = 0.0f;         // radians per ms of sky time
    float rotation_min = 0.0f, rotation_max = 0.0f; // radians
    float duration_min = 0.0f, duration_max = 0.0f; // seconds; 0 = never times out
    float elevation = 0.0f, radius = 0.0f;
    std::int32_t subdivision_x = 1, subdivision_y = 1;
    float visible_angle = 0.0f; // radians each side of the zenith
};

// The [Cloud-<name>] section of weather.ini.
CloudDefinition parse_cloud_definition(const IniFile& weather, std::string_view name);
// The zone's default weather pattern: sky.ini [SkySetting-<zone>]
// DefaultWeather, else [SkySetting-default]'s.
std::string default_weather(const IniFile& sky, std::string_view zone);
// The clouds of a weather pattern (Cloud0, Cloud1, ... until one is missing).
std::vector<CloudDefinition> weather_clouds(const IniFile& weather, std::string_view pattern);
// Both files from the game directory (empty when they are missing).
std::vector<CloudDefinition> load_zone_clouds(const GameDir& game, std::string_view zone);

} // namespace nmsc::assets
