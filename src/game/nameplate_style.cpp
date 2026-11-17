#include "game/nameplate_style.h"

#include <algorithm>

#include "core/config.h"

namespace hailstone::game {
namespace {

int clamp_size(int v) { return std::clamp(v, kPlateFontSizeMin, kPlateFontSizeMax); }
int clamp_far(int v) { return std::clamp(v, kPlateFarScaleMin, kPlateFarScaleMax); }
int clamp_tag(int v) { return std::clamp(v, kPlateTagScaleMin, kPlateTagScaleMax); }
PlateEdge to_edge(int v) {
    return v == static_cast<int>(PlateEdge::Outline) ? PlateEdge::Outline
           : v == static_cast<int>(PlateEdge::None)  ? PlateEdge::None
                                                     : PlateEdge::Shadow;
}

} // namespace

PlateStyle load_plate_style(const Config& config) {
    PlateStyle s;
    s.face = config.get_string("display.plate_font_face", "");
    s.size = clamp_size(config.get_int("display.plate_font_size", kPlateFontSizeDefault));
    s.bold = config.get_bool("display.plate_font_bold", false);
    s.edge = to_edge(config.get_int("display.plate_edge", 0));
    s.far_scale = clamp_far(config.get_int("display.plate_far_scale", kPlateFarScaleMax));
    s.tag_scale = clamp_tag(config.get_int("display.plate_tag_scale", kPlateTagScaleMax));
    return s;
}

void store_plate_style(const PlateStyle& s, Config& config) {
    config.set("display.plate_font_face", s.face);
    config.set_int("display.plate_font_size", s.size);
    config.set_bool("display.plate_font_bold", s.bold);
    config.set_int("display.plate_edge", static_cast<int>(s.edge));
    config.set_int("display.plate_far_scale", s.far_scale);
    config.set_int("display.plate_tag_scale", s.tag_scale);
}

std::optional<int> plate_style_option(const PlateStyle& s, std::string_view name) {
    if (name == "plate_font_size") return s.size;
    if (name == "plate_font_bold") return s.bold ? 1 : 0;
    if (name == "plate_edge") return static_cast<int>(s.edge);
    if (name == "plate_far_scale") return s.far_scale;
    if (name == "plate_tag_scale") return s.tag_scale;
    return std::nullopt;
}

bool set_plate_style_option(PlateStyle& s, std::string_view name, int v) {
    if (name == "plate_font_size") s.size = clamp_size(v);
    else if (name == "plate_font_bold") s.bold = v != 0;
    else if (name == "plate_edge") s.edge = to_edge(v);
    else if (name == "plate_far_scale") s.far_scale = clamp_far(v);
    else if (name == "plate_tag_scale") s.tag_scale = clamp_tag(v);
    else return false;
    return true;
}

float plate_distance_factor(float distance, float range, int far_scale) {
    const float far = static_cast<float>(clamp_far(far_scale)) / 100.0f;
    if (far >= 1.0f || range <= 0.0f) return 1.0f;
    const float t = std::clamp(distance / range, 0.0f, 1.0f);
    return 1.0f + (far - 1.0f) * t;
}

} // namespace hailstone::game
