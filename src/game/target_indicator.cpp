#include "game/target_indicator.h"

#include <algorithm>
#include <cstdlib>
#include <string>

#include "core/ini_file.h"

namespace nmsc::game {

TargetIndicatorSettings TargetIndicatorSettings::parse(const IniFile& ini) {
    TargetIndicatorSettings out;
    for (std::size_t k = 0; k < kTargetIndicatorSections.size(); ++k) {
        std::string_view section = kTargetIndicatorSections[k];
        TargetRingStyle& s = out.styles[k];
        auto byte = [&](std::string_view key, std::uint8_t fallback) {
            return static_cast<std::uint8_t>(ini.get_int(section, key, fallback) & 0xff);
        };
        s.alpha = byte("Alpha", 0xc0);
        s.red = byte("Red", 0xff);
        s.green = byte("Green", 0xff);
        s.blue = byte("Blue", 0xff);
        if (auto v = ini.get(section, "FloorOffset")) s.floor_offset = std::strtof(v->c_str(), nullptr);
    }
    return out;
}

std::optional<int> target_ring_section(Con con) {
    switch (con) {
    case Con::Gray: return 0;
    case Con::Green: return 1;
    case Con::LightBlue: return 2;
    case Con::DarkBlue: return 3;
    case Con::White: return 4;
    case Con::Yellow: return 5;
    case Con::Red: return 6;
    default: return std::nullopt;
    }
}

float target_ring_radius(float avatar_height, float actor_scale, bool has_actor) {
    float r = has_actor ? avatar_height * actor_scale : avatar_height;
    return std::max(r, 2.0f);
}

} // namespace nmsc::game
