#include "game/map_data.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace nmsc::game {

namespace {

// __ftol2_sse: truncation toward zero.
int ftol(float v) {
    if (!(v == v)) return INT_MIN; // NaN: the "integer indefinite" value
    if (v >= 2147483648.0f || v < -2147483648.0f) return INT_MIN;
    return static_cast<int>(v);
}

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

// The pieces of the original's sscanf formats: whitespace in the format skips any whitespace, a literal must match
// the next character, %f / %d skip whitespace and read a number, %256s a word. Stops at the first mismatch.
class Scanner {
public:
    explicit Scanner(std::string_view s) : s_(s) {}
    void skip_space() {
        while (pos_ < s_.size() && is_space(s_[pos_])) ++pos_;
    }
    bool literal(char c) {
        if (pos_ < s_.size() && s_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }
    bool real(float& out) {
        skip_space();
        std::size_t p = pos_;
        bool neg = false;
        if (p < s_.size() && (s_[p] == '+' || s_[p] == '-')) neg = s_[p++] == '-';
        // Digits, a point, digits, an exponent: what the CRT's %f takes.
        std::size_t start = p;
        while (p < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p]))) ++p;
        if (p < s_.size() && s_[p] == '.') {
            ++p;
            while (p < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p]))) ++p;
        }
        if (p == start || (p == start + 1 && s_[start] == '.')) return false;
        if (p < s_.size() && (s_[p] == 'e' || s_[p] == 'E')) {
            std::size_t q = p + 1;
            if (q < s_.size() && (s_[q] == '+' || s_[q] == '-')) ++q;
            if (q < s_.size() && std::isdigit(static_cast<unsigned char>(s_[q]))) {
                while (q < s_.size() && std::isdigit(static_cast<unsigned char>(s_[q]))) ++q;
                p = q;
            }
        }
        std::string text(s_.substr(start, p - start));
        float v = std::strtof(text.c_str(), nullptr);
        out = neg ? -v : v;
        pos_ = p;
        return true;
    }
    bool integer(int& out) {
        skip_space();
        std::size_t p = pos_;
        bool neg = false;
        if (p < s_.size() && (s_[p] == '+' || s_[p] == '-')) neg = s_[p++] == '-';
        std::size_t start = p;
        std::uint64_t v = 0;
        while (p < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p]))) v = v * 10 + static_cast<std::uint64_t>(s_[p++] - '0');
        if (p == start) return false;
        std::uint32_t low = static_cast<std::uint32_t>(v);
        out = static_cast<int>(neg ? 0u - low : low);
        pos_ = p;
        return true;
    }
    bool word(std::string& out, std::size_t max) {
        skip_space();
        std::size_t p = pos_;
        while (p < s_.size() && !is_space(s_[p]) && p - pos_ < max) ++p;
        if (p == pos_) return false;
        out.assign(s_.substr(pos_, p - pos_));
        pos_ = p;
        return true;
    }
    // ", " / ",  " in the formats: the comma, then any whitespace.
    bool comma() {
        if (!literal(',')) return false;
        skip_space();
        return true;
    }

private:
    std::string_view s_;
    std::size_t pos_ = 0;
};

// "%f, %f, %f,  %f, %f, %f,  %d, %d, %d"
void scan_line(std::string_view s, MapLine& l, int& r, int& g, int& b) {
    Scanner sc(s);
    float* f[6] = {&l.start.x, &l.start.y, &l.start.z, &l.end.x, &l.end.y, &l.end.z};
    for (int i = 0; i < 6; ++i) {
        if (!sc.real(*f[i])) return;
        if (!sc.comma()) return;
    }
    int* c[3] = {&r, &g, &b};
    for (int i = 0; i < 3; ++i) {
        if (!sc.integer(*c[i])) return;
        if (i < 2 && !sc.comma()) return;
    }
}

// "%f, %f, %f,  %d, %d, %d,  %d,  %256s"
void scan_label(std::string_view s, MapLabel& l, int& r, int& g, int& b, std::string& text) {
    Scanner sc(s);
    float* f[3] = {&l.pos.x, &l.pos.y, &l.pos.z};
    for (auto* v : f) {
        if (!sc.real(*v)) return;
        if (!sc.comma()) return;
    }
    int* c[4] = {&r, &g, &b, &l.size};
    for (auto* v : c) {
        if (!sc.integer(*v)) return;
        if (!sc.comma()) return;
    }
    std::string w;
    if (sc.word(w, 256)) text = w;
}

std::uint32_t rgb_argb(int r, int g, int b) {
    return 0xff000000u | (static_cast<std::uint32_t>(r & 0xff) << 16) | (static_cast<std::uint32_t>(g & 0xff) << 8) |
           static_cast<std::uint32_t>(b & 0xff);
}

int half(int v) { return v / 2; }

} // namespace

MapLayerParse parse_map_layer(std::string_view text, int layer, std::string& label_buffer) {
    MapLayerParse out;
    MapExtents& e = out.extents;
    std::size_t pos = 0;
    while (pos < text.size()) {
        // fgets(buf, 0x3fe): up to 1021 characters, through the newline.
        std::size_t end = pos;
        while (end < text.size() && end - pos < 1021) {
            if (text[end++] == '\n') break;
        }
        std::string_view line = text.substr(pos, end - pos);
        pos = end;
        if (line.empty()) continue;
        char kind = static_cast<char>(std::toupper(static_cast<unsigned char>(line[0])));
        std::string_view rest = line.size() > 2 ? line.substr(2) : std::string_view();
        if (kind == 'L') {
            MapLine l;
            int r = 0, g = 0, b = 0;
            scan_line(rest, l, r, g, b);
            l.color = rgb_argb(r, g, b);
            l.layer = layer;
            auto widen = [&](float x, float y) {
                if (x < static_cast<float>(e.min_x)) e.min_x = ftol(x);
                if (y < static_cast<float>(e.min_y)) e.min_y = ftol(y);
                if (static_cast<float>(e.max_x) < x) e.max_x = ftol(x);
                if (static_cast<float>(e.max_y) < y) e.max_y = ftol(y);
            };
            widen(l.start.x, l.start.y);
            widen(l.end.x, l.end.y);
            out.lines.push_back(l);
        } else if (kind == 'P') {
            MapLabel l;
            l.size = 0; // the record's size field is not cleared when the field is missing; read as below 1
            int r = 0, g = 0, b = 0;
            scan_label(rest, l, r, g, b, label_buffer);
            l.color = rgb_argb(r, g, b);
            // The buffer's 256th byte is cleared after the scan: at most 255 characters; '_' reads as ' '.
            std::string t = label_buffer.substr(0, 255);
            std::replace(t.begin(), t.end(), '_', ' ');
            l.text = t;
            l.layer = layer;
            l.size = std::clamp(l.size, 1, 3);
            out.labels.push_back(std::move(l));
        }
    }
    return out;
}

std::string map_layer_file_name(std::string_view zone, int layer) {
    std::string name(zone);
    if (layer != 0) name += "_" + std::to_string(layer);
    return name + ".txt";
}

std::array<int, 4> map_label_rect(unsigned anchor, int x, int y, int w, int h) {
    std::uint32_t ux = static_cast<std::uint32_t>(x), uy = static_cast<std::uint32_t>(y);
    std::uint32_t uw = static_cast<std::uint32_t>(w), uh = static_cast<std::uint32_t>(h);
    std::uint32_t right = ux + uw, bottom = uy + uh;
    std::array<std::uint32_t, 4> o{ux, uy, right, bottom};
    if (x > static_cast<int>(right)) {
        o[0] = right;
        o[2] = ux;
    }
    if (y > static_cast<int>(bottom)) {
        o[1] = bottom;
        o[3] = uy;
    }
    if (anchor <= 23) {
        std::uint32_t hh = static_cast<std::uint32_t>(half(h));
        static const std::uint8_t kDx[24] = {0, 0, 1, 1, 1, 2, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2};
        std::uint32_t dx = kDx[anchor] == 0 ? 0u - static_cast<std::uint32_t>(half(w)) : kDx[anchor] == 1 ? 4u : 0xfffffffcu - uw;
        static const std::uint8_t kDy[24] = {0, 1, 2, 3, 4, 2, 3, 4, 5, 6, 5, 6, 7, 8, 7, 8, 9, 10, 9, 10, 11, 12, 11, 12};
        std::uint32_t dy = 0;
        switch (kDy[anchor]) {
        case 0: dy = 0xfffffffeu - uh; break;
        case 1: dy = 2; break;
        case 2: dy = 0u - hh; break;
        case 3: dy = 0u - uh; break;
        case 4: dy = 0; break;
        case 5: dy = 0u - uh - hh; break;
        case 6: dy = hh; break;
        case 7: dy = 0u - 2 * uh; break;
        case 8: dy = uh; break;
        case 9: dy = 0u - 2 * uh - hh; break;
        case 10: dy = hh + uh; break;
        case 11: dy = 0u - 3 * uh; break;
        default: dy = 2 * uh; break;
        }
        o[0] += dx;
        o[2] += dx;
        o[1] += dy;
        o[3] += dy;
    }
    return {static_cast<int>(o[0]), static_cast<int>(o[1]), static_cast<int>(o[2]), static_cast<int>(o[3])};
}

// ---------------------------------------------------------------- zoom and extents

void MapView::set_zone_extents(int lo_x, int hi_x, int lo_y, int hi_y) {
    int width = std::abs(hi_x - lo_x);
    if (zoom == 0.0f) {
        range = 200;
        zoom = static_cast<float>(width) * 0.005f;
    } else if (zoom != 1.0f) {
        // Keep the range: the units across the view stay as they were.
        float shown = std::fabs(max_x - min_x) / zoom;
        float w = static_cast<float>(width);
        if (w <= shown) {
            range = width;
            zoom = 1.0f;
        } else {
            zoom = w / shown;
            range = ftol(shown);
        }
    } else {
        range = width;
    }
    relayout = true;
    min_x = static_cast<float>(lo_x);
    max_x = static_cast<float>(hi_x);
    min_y = static_cast<float>(lo_y);
    max_y = static_cast<float>(hi_y);
}

void MapView::set_zoom(float z) {
    zoom = z;
    float width = std::fabs(max_x - min_x);
    if (z == 1.0f || z == 0.0f) {
        range = ftol(width);
    } else {
        range = ftol(width / z);
        if (range < 5) {
            range = 5;
            zoom = width * 0.2f;
        }
    }
    if (zoom * 1.2f < relayout_zoom || relayout_zoom < zoom * 0.8f) relayout = true;
}

void MapView::wheel(int delta) {
    if (delta > 0) {
        set_zoom(zoom * 1.5f);
    } else if (delta < 0) {
        set_zoom(zoom * 0.6666667f);
        if (zoom < 1.0f) set_zoom(1.0f);
    }
}

void MapView::zoom_in() { set_zoom(zoom * 1.1f); }

void MapView::zoom_out() {
    set_zoom(zoom * 0.9090909f);
    if (zoom < 1.0f) set_zoom(1.0f);
}

// ---------------------------------------------------------------- loading and saving

void MapView::clear() {
    remove_marked_lines();
    remove_marked_label();
    lines.clear();
    labels.clear();
    placed_.clear();
    next_label_id = 0;
    line_active = false;
    marked_lines.clear();
    marked_label = nullptr;
    selected_label = nullptr;
}

void MapView::measure_label(MapLabel& label, const MapMeasure& measure) {
    if (!measure) return;
    auto [w, h] = measure(label.text, label.size);
    label.width = w;
    label.height = h;
}

void MapView::load(const std::function<std::optional<std::string>(int layer)>& read, const MapMeasure& measure) {
    std::string buffer;
    for (int layer = 0; layer < 4; ++layer) {
        auto text = read(layer);
        if (!text) continue;
        MapLayerParse p = parse_map_layer(*text, layer, buffer);
        for (auto& l : p.lines) lines.push_front(l);
        for (auto& l : p.labels) {
            l.id = ++next_label_id;
            measure_label(l, measure);
            labels.push_front(std::move(l));
        }
        if (layer == 0) set_zone_extents(p.extents.min_x, p.extents.max_x, p.extents.min_y, p.extents.max_y);
    }
    relayout = true;
}

void MapView::set_world_box(const glm::vec3& lo, const glm::vec3& hi) {
    // CMapViewWnd_LoadZoneMap: SetZoneExtents(ftol(lo[1]), ftol(hi[1]), ftol(lo[0]), ftol(hi[0])) of the scene's
    // box, whose [0] is the original's Y (this client's world x) and [1] its X (world y).
    set_zone_extents(ftol(lo.y), ftol(hi.y), ftol(lo.x), ftol(hi.x));
}

std::string MapView::layer_text(int layer) const {
    std::string out;
    char buf[1024];
    for (const auto& l : lines) {
        if (l.layer != layer) continue;
        std::snprintf(buf, sizeof(buf), "L %0.4f, %0.4f, %0.4f,  %0.4f, %0.4f, %0.4f,  %d, %d, %d\n",
                      static_cast<double>(l.start.x), static_cast<double>(l.start.y), static_cast<double>(l.start.z),
                      static_cast<double>(l.end.x), static_cast<double>(l.end.y), static_cast<double>(l.end.z),
                      static_cast<int>((l.color >> 16) & 0xff), static_cast<int>((l.color >> 8) & 0xff),
                      static_cast<int>(l.color & 0xff));
        out += buf;
    }
    for (const auto& l : labels) {
        if (l.layer != layer) continue;
        std::string t = l.text.substr(0, 255);
        std::replace(t.begin(), t.end(), ' ', '_');
        std::snprintf(buf, sizeof(buf), "P %0.4f, %0.4f, %0.4f,  %d, %d, %d,  %d,  %s\n", static_cast<double>(l.pos.x),
                      static_cast<double>(l.pos.y), static_cast<double>(l.pos.z), static_cast<int>((l.color >> 16) & 0xff),
                      static_cast<int>((l.color >> 8) & 0xff), static_cast<int>(l.color & 0xff), l.size, t.c_str());
        out += buf;
    }
    return out;
}

bool MapView::line_shown(const MapLine& l, std::optional<float> feet) const {
    if (!layer_shown(l.layer)) return false;
    if (!z_filter || !feet) return true;
    float lo = *feet - static_cast<float>(min_z), hi = *feet + static_cast<float>(max_z);
    return !((l.start.z < lo && l.end.z < lo) || (hi < l.start.z && hi < l.end.z));
}

bool MapView::label_shown(const MapLabel& l, std::optional<float> feet) const {
    if (!layer_shown(l.layer)) return false;
    if (!z_filter || !feet) return true;
    return *feet - static_cast<float>(min_z) <= l.pos.z && l.pos.z <= *feet + static_cast<float>(max_z);
}

float MapView::z_fade(float a, float b, std::optional<float> feet) const {
    if (!auto_z || !feet) return 1.0f;
    const float lo = std::min(a, b), hi = std::max(a, b);
    const float d = *feet < lo ? lo - *feet : hi < *feet ? *feet - hi : 0.0f;
    const float r = static_cast<float>(std::max(auto_z_range, 1));
    if (d <= r) return 1.0f;
    return std::max(kAutoZFloor, 1.0f - (1.0f - kAutoZFloor) * (d - r) / (2.0f * r));
}

// ---------------------------------------------------------------- editing

void MapView::add_line(const glm::vec3& a, const glm::vec3& b, std::uint32_t color, int layer) {
    MapLine l;
    l.start = a;
    l.end = b;
    l.color = color;
    l.layer = layer;
    lines.push_front(l);
}

void MapView::add_label(const glm::vec3& pos, std::uint32_t argb, int size, const std::string& text,
                        const MapMeasure& measure) {
    if (!current_zone) return;
    MapLabel l;
    l.id = ++next_label_id;
    l.pos = pos;
    l.color = argb;
    l.size = size;
    l.text = text;
    l.layer = active_layer;
    measure_label(l, measure);
    labels.push_front(std::move(l));
}

void MapView::start_line(const glm::vec3& p) {
    if (!current_zone) return;
    line_active = true;
    current_point = p;
}

void MapView::add_point(const glm::vec3& p) {
    // The original tests the current point against the zone box's max plus p (FUN_006cab70 @0x6cab8d), not p.
    if (!line_active || !current_zone) return;
    if (!(0.9f < std::fabs(current_point.x - (max_x + p.x)) || 0.9f < std::fabs(current_point.y - (max_y + p.y))))
        return;
    add_line(current_point, p, current_color, active_layer);
    current_point = p;
}

void MapView::end_line(const glm::vec3& p) {
    add_point(p);
    line_active = false;
}

void MapView::remove_marked_lines() {
    for (MapLine* m : marked_lines)
        lines.remove_if([m](const MapLine& l) { return &l == m; });
    marked_lines.clear();
}

void MapView::remove_marked_label() {
    if (!marked_label) return;
    MapLabel* m = marked_label;
    if (selected_label == m) selected_label = nullptr;
    labels.remove_if([m](const MapLabel& l) { return &l == m; });
    marked_label = nullptr;
    placed_.clear();
}

void MapView::clear_active_layer() {
    int layer = active_layer;
    // (The original leaves the marks pointing at freed records; they are dropped here.)
    std::erase_if(marked_lines, [layer](const MapLine* l) { return l->layer == layer; });
    if (marked_label && marked_label->layer == layer) marked_label = nullptr;
    if (selected_label && selected_label->layer == layer) selected_label = nullptr;
    lines.remove_if([layer](const MapLine& l) { return l.layer == layer; });
    labels.remove_if([layer](const MapLabel& l) { return l.layer == layer; });
    placed_.clear();
}

void MapView::mark_line(MapLine* line) {
    if (!line) {
        marked_lines.clear();
        return;
    }
    if (!is_marked(line)) marked_lines.push_front(line);
}

bool MapView::is_marked(const MapLine* line) const {
    return std::find(marked_lines.begin(), marked_lines.end(), line) != marked_lines.end();
}

bool MapView::join_lines_at_intersect(bool ctrl) {
    if (marked_lines.size() != 2) return false;
    MapLine& b = *marked_lines[0]; // the newer mark
    MapLine& a = *marked_lines[1]; // the older: the one moved with Ctrl
    // FUN_006c8d30: where the two lines (as infinite lines) cross; parallel within 0.0001: none.
    double x1 = a.start.x, y1 = a.start.y, x2 = a.end.x, y2 = a.end.y;
    double x3 = b.start.x, y3 = b.start.y, x4 = b.end.x, y4 = b.end.y;
    double den = (y4 - y3) * (x2 - x1) - (x4 - x3) * (y2 - y1);
    if (std::fabs(den) < 0.0001) return false;
    double t = ((y1 - y3) * (x4 - x3) - (x1 - x3) * (y4 - y3)) / den;
    float ix = static_cast<float>((x2 - x1) * t + x1), iy = static_cast<float>(t * (y2 - y1) + y1);
    auto d2 = [&](const glm::vec3& p) { return (p.x - ix) * (p.x - ix) + (p.y - iy) * (p.y - iy); };
    float da0 = d2(a.start), da1 = d2(a.end);
    auto move_nearer = [&](MapLine& l, float d0, float d1) {
        glm::vec3& p = d1 <= d0 ? l.end : l.start;
        p.x = ix;
        p.y = iy;
    };
    if (ctrl) {
        if (!(da0 <= 250000.0f || da1 <= 250000.0f)) return false;
        move_nearer(a, da0, da1);
        return true;
    }
    float db0 = d2(b.start), db1 = d2(b.end);
    if (!(da0 <= 250000.0f || da1 <= 250000.0f || db0 <= 250000.0f || db1 <= 250000.0f)) return false;
    move_nearer(a, da0, da1);
    move_nearer(b, db0, db1);
    return true;
}

void MapView::set_marked_lines_color(std::uint32_t rgb) {
    for (MapLine* l : marked_lines) l->color = 0xff000000u | (rgb & 0xffffff);
}

MapLabel* MapView::label_by_id(int id) {
    for (auto& l : labels)
        if (l.id == id) return &l;
    return nullptr;
}

// ---------------------------------------------------------------- each frame

void MapView::precalc(const std::optional<glm::vec2>& player, const std::optional<glm::vec4>& label_box,
                      const std::optional<glm::vec4>& find_box) {
    float w = max_x - min_x, h = max_y - min_y;
    scale_x = 472.0f / w;
    scale_y = 360.0f / h;
    scale_diff_x = scale_diff_y = 0.0f;
    if (scale_y <= scale_x) {
        float a = std::fabs(w);
        scale_diff_x = (a * scale_y - a * scale_x) * -0.5f;
        scale_x = scale_y;
    } else {
        float a = std::fabs(h);
        scale_diff_y = (a * scale_x - a * scale_y) * -0.5f;
        scale_y = scale_x;
    }
    line_offset_x = max_x;
    line_offset_y = max_y;
    if (current_zone && find_box) {
        line_offset_x = max_x - ((*find_box)[2] + (*find_box)[3]) * 0.5f;
        line_offset_y = max_y - ((*find_box)[0] + (*find_box)[1]) * 0.5f;
    } else if (current_zone && player && !selected_label) {
        line_offset_x = max_x + player->x; // max - the original's X, the player at (-X, -Y)
        line_offset_y = max_y + player->y;
    } else if (selected_label && label_box) {
        // the box's (minY, maxY, minX, maxX) in the original's axes
        line_offset_x = max_x - ((*label_box)[2] + (*label_box)[3]) * 0.5f;
        line_offset_y = max_y - ((*label_box)[0] + (*label_box)[1]) * 0.5f;
    }
    line_offset_x *= zoom;
    line_offset_y *= zoom;
}

glm::vec2 MapView::transform(float x, float y, float cw, float ch) const {
    x += max_x;
    y += max_y;
    if (zoom != 1.0f) {
        x = x * zoom - line_offset_x;
        y = y * zoom - line_offset_y;
    }
    x *= scale_x;
    y *= scale_y;
    if (zoom == 1.0f) {
        x += scale_diff_x;
        y += scale_diff_y;
    } else {
        x += cw * 0.5f;
        y += ch * 0.5f;
    }
    return {x + static_cast<float>(pan_x), y + static_cast<float>(pan_y)};
}

glm::vec2 MapView::world_coordinates(float px, float py, float cw, float ch) const {
    float x = px - static_cast<float>(pan_x), y = py - static_cast<float>(pan_y);
    if (zoom == 1.0f) {
        x -= scale_diff_x;
        y -= scale_diff_y;
    } else {
        x -= cw * 0.5f;
        y -= ch * 0.5f;
    }
    x /= scale_x;
    y /= scale_y;
    if (zoom != 1.0f) {
        x = (line_offset_x + x) / zoom;
        y = (line_offset_y + y) / zoom;
    }
    x -= max_x;
    y -= max_y;
    return {-y, -x}; // the original's (Y, X)
}

int MapView::count_overlaps(int l, int t, int r, int b, const Placed* exclude) const {
    int n = 0;
    for (const Placed& p : placed_) {
        if (&p == exclude) continue;
        if (r < p.l || l > p.r || t > p.b || b < p.t) continue;
        int il = std::max(l, p.l), it = std::max(t, p.t), ir = std::min(r, p.r), ib = std::min(b, p.b);
        if (il == -1 && it == -1 && ir == -1 && ib == -1) continue;
        if (t == p.b || b == p.t || l == p.r || r == p.l) continue;
        ++n;
    }
    return n;
}

// FUN_006cada0: tries anchors from `anchor` up; the first whose rectangle lies inside the clip rectangle (or any,
// with `force`) and overlaps nothing is taken (placed, the label's offset set, 0 returned). Otherwise the anchor
// with the fewest overlaps (under 50) as -(anchor + 1), or 1 when every anchor has 50 or more.
int MapView::try_place(unsigned anchor, int x, int y, MapLabel& label, int cl, int ct, int cr, int cb, bool force) {
    for (; anchor < 24; ++anchor) {
        auto r = map_label_rect(anchor, x, y, label.width, label.height);
        int il = -1, it = -1, ir = -1, ib = -1;
        if (cl <= r[2] && r[0] <= cr && r[1] <= cb && ct <= r[3]) {
            il = std::max(r[0], cl);
            it = std::max(r[1], ct);
            ir = std::min(r[2], cr);
            ib = std::min(r[3], cb);
        }
        bool inside = (il != -1 || it != -1 || ir != -1 || ib != -1) && ir - il == r[2] - r[0] && ib - it == r[3] - r[1];
        if ((force || inside) && count_overlaps(r[0], r[1], r[2], r[3], nullptr) == 0) {
            placed_.push_front({r[0], r[1], r[2], r[3], &label, true});
            label.offset_x = r[0] - x;
            label.offset_y = r[1] - y;
            return 0;
        }
    }
    int best = 50, result = -1;
    for (unsigned a = 0; a < 24; ++a) {
        auto r = map_label_rect(a, x, y, label.width, label.height);
        int n = count_overlaps(r[0], r[1], r[2], r[3], nullptr);
        if (n < best) {
            result = static_cast<int>(a) + 1;
            best = n;
            if (n == 0) break;
        }
    }
    return -result;
}

void MapView::place_pass() {
    for (Placed& p : placed_) {
        if (!p.text || !p.label) continue;
        MapLabel& l = *p.label;
        int x = p.l - l.offset_x, y = p.t - l.offset_y;
        int best = 50, anchor = -1;
        for (unsigned a = 0; a < 24; ++a) {
            auto r = map_label_rect(a, x, y, l.width, l.height);
            int n = count_overlaps(r[0], r[1], r[2], r[3], &p);
            if (n < best) {
                best = n;
                anchor = static_cast<int>(a);
                if (n == 0) break;
            }
        }
        if (anchor < 0) continue;
        auto r = map_label_rect(static_cast<unsigned>(anchor), x, y, l.width, l.height);
        p.l = r[0];
        p.t = r[1];
        p.r = r[2];
        p.b = r[3];
        l.offset_x = p.l - x;
        l.offset_y = p.t - y;
    }
}

void MapView::place_labels(float cw, float ch, int ox, int oy, int clip_r, int clip_b) {
    if (labels.empty()) return;
    placed_.clear();
    // The points: 3 x 3 boxes.
    for (MapLabel& l : labels) {
        if (!layer_shown(l.layer)) continue;
        glm::vec2 p = transform(l.pos.x, l.pos.y, cw, ch);
        float fx = static_cast<float>(ox) + p.x, fy = static_cast<float>(oy) + p.y;
        int b = ftol(fy + 1.0f), r = ftol(fx + 1.0f), t = ftol(fy - 1.0f), le = ftol(fx - 1.0f);
        placed_.push_front({std::min(le, r), std::min(t, b), std::max(le, r), std::max(t, b), &l, false});
    }
    // The texts.
    for (MapLabel& l : labels) {
        l.offset_x = l.offset_y = 0;
        if (!layer_shown(l.layer) || !labels_active) continue;
        glm::vec2 p = transform(l.pos.x, l.pos.y, cw, ch);
        float fx = static_cast<float>(ox) + p.x, fy = static_cast<float>(oy) + p.y;
        int x = ftol(fx), y = ftol(fy);
        int res = try_place(0, x, y, l, ox, oy, clip_r, clip_b, true);
        if (res < 0) res = try_place(0, x, y, l, ox, oy, clip_r, clip_b, true);
        if (res < 0) {
            auto r = map_label_rect(static_cast<unsigned>(-1 - res), x, y, l.width, l.height);
            placed_.push_front({r[0], r[1], r[2], r[3], &l, true});
            l.offset_x = ftol(static_cast<float>(r[0]) - fx);
            l.offset_y = ftol(static_cast<float>(r[1]) - fy);
        }
    }
    place_pass();
    place_pass();
    place_pass();
}

void MapView::zoom_to_fit(float lo_y, float hi_y, float lo_x, float hi_x, bool find, float cw, float ch) {
    float out = find ? 0.85f : 0.8f, in = find ? 0.7f : 0.6f, most = find ? 20.0f : 10.0f;
    pan_x = pan_y = 0;
    glm::vec2 a = transform(-hi_x, -hi_y, cw, ch);
    glm::vec2 b = transform(-lo_x, -lo_y, cw, ch);
    float f = 1.0f;
    int w = static_cast<int>(cw), h = static_cast<int>(ch);
    if (w > 0 && h > 0) f = std::max(std::fabs(b.x - a.x) / static_cast<float>(w), std::fabs(b.y - a.y) / static_cast<float>(h));
    if (out < f) {
        float z = zoom / (f / out);
        set_zoom(z < 1.0f ? 1.0f : z);
    } else if (f < in) {
        float z = zoom / (f / in);
        set_zoom(most < z ? most : z);
    }
}

} // namespace nmsc::game
