#include "assets/sky_weather.h"

#include <cstdlib>

#include "core/game_dir.h"
#include "core/ini_file.h"
#include "core/saturate.h"

namespace nmsc::assets {

namespace {

float number(const IniFile& ini, const std::string& section, std::string_view key, float fallback = 0.0f) {
    auto v = ini.get(section, key);
    if (!v || v->empty()) return fallback;
    return std::strtof(v->c_str(), nullptr);
}

} // namespace

CloudDefinition parse_cloud_definition(const IniFile& w, std::string_view name) {
    CloudDefinition c;
    c.name = std::string(name);
    std::string s = "Cloud-" + c.name;
    c.texture = w.get_string(s, "Texture");
    c.color_set = w.get_string(s, "ColorSet");
    c.desired_count = saturate_cast<std::int32_t>(number(w, s, "DesiredCount"));
    c.spawn_interval_min = number(w, s, "SpawnIntervalMin");
    c.spawn_interval_max = number(w, s, "SpawnIntervalMax");
    c.size_min = number(w, s, "SizeMin");
    c.size_max = number(w, s, "SizeMax");
    c.offset_min = number(w, s, "OffsetMin");
    c.offset_max = number(w, s, "OffsetMax");
    c.velocity_min = number(w, s, "VelocityMin");
    c.velocity_max = number(w, s, "VelocityMax");
    c.spin_min = number(w, s, "SpinMin");
    c.spin_max = number(w, s, "SpinMax");
    c.rotation_min = number(w, s, "RotationMin");
    c.rotation_max = number(w, s, "RotationMax");
    c.duration_min = number(w, s, "DurationMin");
    c.duration_max = number(w, s, "DurationMax");
    c.elevation = number(w, s, "Elevation");
    c.radius = number(w, s, "Radius");
    c.subdivision_x = saturate_cast<std::int32_t>(number(w, s, "SubdivisionX", 1.0f));
    c.subdivision_y = saturate_cast<std::int32_t>(number(w, s, "SubdivisionY", 1.0f));
    c.visible_angle = number(w, s, "VisibleAngle");
    return c;
}

std::string default_weather(const IniFile& sky, std::string_view zone) {
    std::string w = sky.get_string("SkySetting-" + std::string(zone), "DefaultWeather");
    if (w.empty()) w = sky.get_string("SkySetting-default", "DefaultWeather");
    return w;
}

std::vector<CloudDefinition> weather_clouds(const IniFile& weather, std::string_view pattern) {
    std::vector<CloudDefinition> out;
    std::string section = "WeatherPattern-" + std::string(pattern);
    for (int i = 0;; ++i) {
        auto name = weather.get(section, "Cloud" + std::to_string(i));
        if (!name) break;
        if (name->empty()) continue;
        auto def = parse_cloud_definition(weather, *name);
        if (!def.texture.empty() && def.desired_count > 0) out.push_back(std::move(def));
    }
    return out;
}

std::vector<CloudDefinition> load_zone_clouds(const GameDir& game, std::string_view zone) {
    IniFile sky, weather;
    auto sky_path = game.find("Resources/Sky/sky.ini");
    auto weather_path = game.find("Resources/Sky/weather.ini");
    if (!sky_path || !weather_path || !sky.load(*sky_path) || !weather.load(*weather_path)) return {};
    return weather_clouds(weather, default_weather(sky, zone));
}

} // namespace nmsc::assets
