#include "game/player_path.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>

#include "core/ini_file.h"
#include "core/msvc_atoi.h"

namespace hailstone::game {

namespace {

constexpr std::size_t kMaxPoints = 10000; // HandleFindPersonReply's limit on the points it adds
constexpr int kMaxMovers = 20;            // +0x38c is capped at 0x14

float dist2(const glm::vec3& a, const glm::vec3& b) {
    glm::vec3 d = a - b;
    return d.x * d.x + d.y * d.y + d.z * d.z;
}

// D3DXVec3CatmullRom: between v1 (s = 0) and v2 (s = 1).
glm::vec3 catmull_rom(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, const glm::vec3& v3, float s) {
    float s2 = s * s, s3 = s2 * s;
    return 0.5f * (2.0f * v1 + (v2 - v0) * s + (2.0f * v0 - 5.0f * v1 + 4.0f * v2 - v3) * s2 +
                   (3.0f * v1 - v0 - 3.0f * v2 + v3) * s3);
}

// The two ordered values of a pair (the loader's min / max swaps).
std::pair<float, float> ordered(float a, float b) { return b <= a ? std::pair{b, a} : std::pair{a, b}; }

} // namespace

PathSettings load_path_settings(const IniFile* ini) {
    PathSettings s;
    if (!ini) return s;
    const std::string_view sec = "Path";
    auto num = [&](std::string_view key, float fallback) {
        auto v = ini->get(sec, key);
        return v && !v->empty() ? std::strtof(v->c_str(), nullptr) : fallback;
    };
    auto integer = [&](std::string_view key, int fallback) {
        auto v = ini->get(sec, key);
        return v && !v->empty() ? core::msvc_atoi(*v) : fallback;
    };
    s.segment_length = num("SegmentLength", 5.0f);
    s.lod_factor = num("LODFactor", 0.0f);
    s.control_segment_length = num("ControlSegmentLength", 5.0f);
    s.merge_distance = num("MergeDistance", 5.0f);
    s.finish_range = num("FinishRange", 10.0f);
    s.texture = ini->get_string(sec, "Texture");
    s.frame_count = integer("FrameCount", 0);
    s.frame_ms = integer("Duration", 0);
    s.texture_scale = num("TextureScale", 1.0f);
    s.texture_speed = num("TextureSpeed", 0.0f);
    s.alpha = static_cast<std::uint8_t>(integer("Alpha", 0x80));
    std::tie(s.fade_start, s.fade_end) = ordered(num("FadeStart", 80.0f), num("FadeEnd", 110.0f));
    std::tie(s.opaque_start, s.opaque_end) = ordered(num("OpaqueStart", 10.0f), num("OpaqueEnd", 20.0f));
    s.initial_length = num("InitialLength", 1.0f);
    s.grow_speed = num("GrowSpeed", 1.0f);
    std::tie(s.scale_min, s.scale_max) = ordered(num("ScaleMin", 0.5f), num("ScaleMax", 1.5f));
    s.scale_speed = num("ScaleSpeed", 1.0f);
    s.sine_period = {num("SinePeriodX", 0), num("SinePeriodY", 0), num("SinePeriodZ", 0)};
    s.sine_radius = {num("SineRadiusX", 0), num("SineRadiusY", 0), num("SineRadiusZ", 0)};
    s.sine_speed = {num("SineSpeedX", 0), num("SineSpeedY", 0), num("SineSpeedZ", 0)};
    // The original reads SineOffsetX, then SineOffsetZ twice (no SineOffsetY).
    s.sine_offset = {num("SineOffsetX", 0), num("SineOffsetZ", 0), num("SineOffsetZ", 0)};
    s.floor_offset = num("FloorOffset", 1.0f);
    s.twist_factor = num("TwistFactor", 0.0f);
    s.twist_speed = num("TwistSpeed", 1.0f);
    s.curve_scale = num("CurveScale", 20.0f);
    s.emitter_speed = num("EmitterSpeed", 1.0f);
    s.emitter_period = integer("EmitterPeriod", 1000);
    s.emitter_start = num("EmitterStartCoordinate", 10.0f);
    s.max_emitters = std::clamp(integer("MaxEmitterCount", 0), 0, kMaxMovers);
    s.sound_distance = num("SoundDistance", 10.0f);
    s.sound_loop_volume = num("SoundLoopVolume", 4.0f);
    s.sound_loop_speed = num("SoundLoopSpeed", 1.0f);
    auto b = ini->get(sec, "Additive");
    s.additive = !b || b->empty() || core::msvc_atoi(*b) != 0 || *b == "true" || *b == "TRUE";
    // PlayerPath_LoadEmitterSettings / LoadFinishEmitterSettings: [Path] Emitter<n> / FinishEmitter<n> name a
    // section with Definition (-1), LifeSpan (10, as float then truncated), Density (1), Opacity (1).
    auto emitter = [&](const std::string& key) {
        PathEmitterSettings e;
        std::string section = ini->get_string(sec, key);
        auto get = [&](std::string_view k) { return ini->get(section, k); };
        if (auto v = get("Definition"); v && !v->empty()) e.definition = core::msvc_atoi(*v);
        if (auto v = get("LifeSpan"); v && !v->empty()) e.life_span = static_cast<int>(std::strtof(v->c_str(), nullptr));
        if (auto v = get("Density"); v && !v->empty()) e.density = std::strtof(v->c_str(), nullptr);
        if (auto v = get("Opacity"); v && !v->empty()) e.opacity = std::strtof(v->c_str(), nullptr);
        return e;
    };
    for (int i = 0; i < 4; ++i) {
        s.emitters[static_cast<std::size_t>(i)] = emitter("Emitter" + std::to_string(i));
        s.finish_emitters[static_cast<std::size_t>(i)] = emitter("FinishEmitter" + std::to_string(i));
    }
    return s;
}

void PlayerPath::set_settings(const PathSettings& s, float avatar_height) {
    settings_ = s;
    floor_offset_ = s.floor_offset * avatar_height;
    movers_.clear(); // PlayerPath_DestroyEmitters
}

// 0x59a910 (and the same fields in PlayerPath_Stop, the failed reply and PlayerPath_Update's zone change).
void PlayerPath::clear() {
    active_ = false;
    face_ = false;
    item_a_ = item_b_ = -1;
    movers_.clear();
    line_.clear();
}

void PlayerPath::stop() { clear(); }

void PlayerPath::target(FindKind kind, std::int32_t id) {
    stop();
    kind_ = kind;
    id_ = id;
    item_a_ = item_b_ = -1;
    point_ = glm::vec3(0.0f);
}

FindRequest PlayerPath::request(std::uint32_t now_ms) {
    // Send_OP_FindPersonRequest: then +0x48 = now, and the path is off (+0, +0x14) until the reply.
    FindRequest r;
    r.kind = kind_;
    r.id = static_cast<std::uint16_t>(id_);
    r.item_a = item_a_;
    r.item_b = item_b_;
    r.point = point_;
    request_ms_ = now_ms;
    active_ = false;
    finished_ = false;
    return r;
}

FindRequest PlayerPath::find_spawn(std::uint16_t id, bool face, std::uint32_t now_ms) {
    target(FindKind::Spawn, id);
    face_ = face;
    return request(now_ms);
}

FindRequest PlayerPath::find_map_location(const glm::vec3& point, bool face, std::uint32_t now_ms) {
    target(FindKind::MapLocation, -1);
    face_ = face;
    point_ = point;
    return request(now_ms);
}

FindRequest PlayerPath::find_target(FindKind kind, std::int32_t id, bool face, std::uint32_t now_ms) {
    target(kind, id);
    face_ = face;
    return request(now_ms);
}

FindRequest PlayerPath::find_point(const glm::vec3& point, bool face, std::uint32_t now_ms) {
    target(FindKind::Point, -1);
    face_ = face;
    point_ = point;
    return request(now_ms);
}

FindRequest PlayerPath::find_plot(std::int32_t plot, bool face, std::uint32_t now_ms) {
    target(FindKind::RealEstatePlot, -1);
    item_a_ = plot; // +0x34; +0x38 stays -1 (Stop set it)
    face_ = face;
    return request(now_ms);
}

std::optional<FindRequest> PlayerPath::refresh_if_moved(const glm::vec3& player, std::uint32_t now_ms) {
    if (request_ms_ + 3000 > now_ms || points_.empty()) return std::nullopt;
    const glm::vec3& at = points_[static_cast<std::size_t>(std::clamp(current_, 0, static_cast<int>(points_.size()) - 1))];
    float d = dist2(at, player);
    if (d < 625.0f) return std::nullopt; // 25 squared (a NaN asks again)
    return request(now_ms);
}

PlayerPath::Reply PlayerPath::handle_reply(std::span<const glm::vec3> points, std::uint32_t now_ms, bool gm,
                                           const FloorFn& floor) {
    Reply out;
    const std::size_t count = points.size();
    if (count < 2) {
        // 0x599f60: a point within FLT_EPSILON of (0, 0, 0) is no point.
        if (count == 1) {
            const glm::vec3& p = points[0];
            constexpr float e = 1.1920929e-07f;
            bool zero = -e < p.x && p.x < e && -e < p.y && p.y < e && -e < p.z && p.z < e;
            if (!zero) {
                out.failed_point = p;
                out.failed_kind = kind_;
            }
        }
        out.failed = true;
        stop(); // without the stop sound
        points_.clear();
        return out;
    }
    active_ = false;
    finished_ = false;
    dest_ = points[0];
    points_.clear();
    // The floor under a point: 0x8d4120 (a sphere dropped from 1 above), else 0x8d42e0 (within 50 above).
    auto on_floor = [&](const glm::vec3& p) -> std::optional<float> {
        if (gm || !floor) return std::nullopt;
        if (auto f = floor(p, 1.0f)) return *f + floor_offset_;
        if (auto f = floor(p, 50.0f)) return *f + floor_offset_;
        return std::nullopt;
    };
    const float merge2 = settings_.merge_distance * settings_.merge_distance;
    const float control = settings_.control_segment_length;
    glm::vec3 prev(FLT_MAX);
    // From the reply's last point back to its second: each on the floor, closer than MergeDistance to the last one
    // kept dropped, and gaps longer than ControlSegmentLength filled with points every that much (on the floor when
    // the floor is within 1000).
    for (std::size_t i = count; i > 1; --i) {
        glm::vec3 p = points[i - 1];
        if (auto z = on_floor(p)) p.z = *z;
        glm::vec3 d = p - prev;
        float len2 = d.x * d.x + d.y * d.y + d.z * d.z;
        if (!(merge2 <= len2)) continue;
        if (i < count && control * control < len2) {
            float step = control / std::sqrt(len2);
            for (float t = step; t < 1.0f - step; t += step) {
                if (points_.size() > kMaxPoints) {
                    out.failed = true;
                    clear();
                    points_.clear();
                    return out; // kStrPathPrevented; the path stays off
                }
                glm::vec3 q = prev + d * t;
                if (auto z = on_floor(q); z && std::fabs(*z - q.z) < 1000.0f) q.z = *z;
                points_.push_back(q);
            }
        }
        points_.push_back(p);
        prev = p;
    }
    tail_ = points_.back();
    current_ = static_cast<int>(points_.size()) - 1;
    active_ = true;
    trim_index_ = 0;
    grown_ = settings_.initial_length - (settings_.fade_end - settings_.fade_start);
    trim_distance_ = 0.0f;
    frame_ms_ = now_ms;
    frame_known_ = true;
    mover_ms_ = 0;
    trim_ms_ = now_ms - 500;
    trim_from_ = glm::vec3(FLT_MAX);
    phase_ = settings_.sine_offset;
    out.started = true;
    if (face_) {
        // The mean of the reply's points 1 .. 3 (as many as there are).
        glm::vec3 sum = points[1];
        int n = 1;
        for (std::size_t i = 2; i < count && i <= 3; ++i, ++n) sum += points[i];
        out.face = sum / static_cast<float>(n);
        face_ = false;
    }
    return out;
}

// 0x59cfa0: every 500 ms that the player moved more than 2, the path is cut after the lowest point (from 1) within
// OpaqueStart of the player; then it is extended to the player (points every ControlSegmentLength) or its last point
// is moved there.
void PlayerPath::trim(const glm::vec3& player) {
    if (frame_ms_ - trim_ms_ > 500 && dist2(player, trim_from_) > 4.0f) {
        trim_ms_ = frame_ms_;
        trim_from_ = player;
        const int n = static_cast<int>(points_.size());
        const int last = n - 1;
        if (trim_index_ == 0) trim_index_ = last;
        const float near2 = settings_.opaque_start * settings_.opaque_start;
        for (int i = n - 1; i >= 1; --i)
            if (dist2(points_[static_cast<std::size_t>(i)], player) <= near2) trim_index_ = i;
        int keep = trim_index_ + 1;
        if (keep < last) {
            points_.resize(static_cast<std::size_t>(keep));
            tail_ = points_[static_cast<std::size_t>(trim_index_)];
            if (trim_index_ < current_) current_ = trim_index_;
        }
    }
    glm::vec3 d = player - tail_;
    float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (settings_.control_segment_length < len) {
        float step = settings_.control_segment_length / len;
        for (float t = step; t < 1.0f - step; t += step) points_.push_back(tail_ + d * t);
        points_.push_back(player);
        tail_ = player;
        return;
    }
    points_.back() = player;
}

// 0x59a5f0: the movers slide toward the destination at EmitterSpeed; every EmitterPeriod a new one starts
// EmitterStartCoordinate short of the trim point (or the oldest is reused).
void PlayerPath::move_emitters(std::uint32_t now_ms, std::uint32_t dt) {
    if (settings_.max_emitters == 0) return;
    for (auto& m : movers_) m.coordinate -= static_cast<float>(dt) * settings_.emitter_speed;
    std::uint32_t elapsed = now_ms - mover_ms_;
    if (static_cast<std::uint32_t>(settings_.emitter_period) >= elapsed) return;
    mover_ms_ = now_ms;
    Mover* m = nullptr;
    if (movers_.size() < static_cast<std::size_t>(settings_.max_emitters)) {
        m = &movers_.emplace_back();
    } else {
        m = &*std::min_element(movers_.begin(), movers_.end(),
                               [](const Mover& a, const Mover& b) { return a.born_ms < b.born_ms; });
    }
    m->born_ms = now_ms;
    m->serial = next_serial_++;
    m->shown = true;
    m->placed = false;
    auto late = static_cast<std::int32_t>(elapsed - static_cast<std::uint32_t>(settings_.emitter_period));
    m->coordinate = (trim_distance_ - settings_.emitter_start) - static_cast<float>(late) * settings_.emitter_speed;
}

int PlayerPath::line_alpha(float distance) const {
    // 0x59b040: (1 - fade) x opaque x Alpha, rounded.
    float opaque = (distance - settings_.opaque_start) / (settings_.opaque_end - settings_.opaque_start);
    opaque = opaque > 0.0f ? std::min(opaque, 1.0f) : 0.0f;
    float range = fade_far_ - fade_near_;
    float fade = range != 0.0f ? (distance - fade_near_) / range : (distance > fade_near_ ? 1.0f : 0.0f);
    fade = fade > 0.0f ? std::min(fade, 1.0f) : 0.0f;
    return static_cast<int>(std::lround((1.0f - fade) * opaque * static_cast<float>(settings_.alpha)));
}

PlayerPath::Update PlayerPath::update(const glm::vec3& player, std::uint32_t now_ms, std::uint32_t zone, bool gm,
                                      const FloorFn& floor) {
    Update out;
    if (zone != zone_) {
        clear();
        zone_ = zone;
    }
    if (!active_ || points_.empty()) {
        frame_ms_ = now_ms;
        frame_known_ = true;
        line_.clear();
        return out;
    }
    const std::uint32_t dt = frame_known_ ? now_ms - frame_ms_ : 0;
    if (kind_ == FindKind::MapLocation) dest_.z = player.z;
    if (finished_ || dist2(player, dest_) < settings_.finish_range * settings_.finish_range) {
        // PlayerPath_OnDestinationReached (only once: +0x14).
        if (!finished_) {
            out.arrived = true;
            finished_ = true;
            active_ = false;
            line_.clear();
            out.finish_at = points_.front();
            if (kind_ == FindKind::MapLocation) out.finish_at.z = dest_.z;
            out.finish_at.z -= floor_offset_;
        }
        return out;
    }
    // The line grows from the player (+0x130) up to FadeStart, then shows FadeStart .. FadeEnd.
    grown_ += static_cast<float>(dt) * settings_.grow_speed;
    float near = grown_, far = settings_.fade_end - settings_.fade_start + grown_;
    if (grown_ > settings_.fade_start) {
        near = settings_.fade_start;
        far = settings_.fade_end;
    }
    std::tie(fade_near_, fade_far_) = ordered(near, far);
    // The player's point for the line's fading: on the floor (CDisplay::GetFloorHeight + FloorOffset) unless a GM or
    // further than 6 from the player's height.
    glm::vec3 eye = player;
    if (!gm && floor)
        if (auto f = floor(player, 1.0f); f && std::fabs(*f + floor_offset_ - player.z) <= 6.0f) eye.z = *f + floor_offset_;
    trim(player);
    move_emitters(now_ms, dt);
    walk(eye, now_ms);
    // The texture, twist and sine phases advance with the frame's time.
    const float t = static_cast<float>(dt);
    uv_ += settings_.texture_speed * t;
    twist_ += settings_.twist_speed * t;
    phase_ += settings_.sine_speed * t;
    frame_ms_ = now_ms;
    // 0x59a7c0: movers off the path stop emitting.
    float total = total_;
    for (auto& m : movers_) m.shown = !(total < m.coordinate || m.coordinate < 0.0f);
    return out;
}

// PlayerPath_Update's drawing loop: the segments from the destination (point k - 1 to point k), measured from the
// destination; those up to the trim point drawn as a spline (or, shorter than the step, only placing the movers).
void PlayerPath::walk(const glm::vec3& eye, std::uint32_t now_ms) {
    line_.clear();
    const int last = static_cast<int>(points_.size()) - 1;
    float along = 0.0f;
    auto at = [&](int i) -> const glm::vec3& { return points_[static_cast<std::size_t>(std::clamp(i, 0, last))]; };
    const auto& s = settings_;
    // 0x59b040's point: the sine wobble at its distance along the path, its alpha and scale.
    auto wobble = [&](const glm::vec3& p, float coord) {
        return p + glm::vec3(std::sin(s.sine_period.x * coord + phase_.x) * s.sine_radius.x,
                             std::sin(s.sine_period.y * coord + phase_.y) * s.sine_radius.y,
                             std::sin(s.sine_period.z * coord + phase_.z) * s.sine_radius.z);
    };
    auto add_point = [&](const glm::vec3& p, float coord) {
        float d = std::sqrt(dist2(p, eye));
        LinePoint lp;
        lp.position = wobble(p, coord);
        lp.alpha = static_cast<float>(line_alpha(d)) / 255.0f;
        lp.coordinate = coord;
        float twist = (twist_ + coord) * s.twist_factor + static_cast<float>(now_ms) * s.twist_speed * 0.001f;
        float period = (uv_ + coord) * s.texture_scale;
        lp.scale = std::sin(period != 0.0f ? std::fmod(twist, period) : twist) * (s.scale_max - s.scale_min) + s.scale_min;
        line_.push_back(lp);
    };
    // 0x59ab60: the movers whose coordinate is on [a, b) go on the curve v0 .. v3 there.
    auto place = [&](float a, float b, const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, const glm::vec3& v3) {
        for (auto& m : movers_) {
            if (!(a <= m.coordinate && m.coordinate < b)) continue;
            m.position = wobble(catmull_rom(v0, v1, v2, v3, (m.coordinate - a) / (b - a)), m.coordinate);
            m.placed = true;
        }
    };
    for (int k = 0; k <= last; ++k) {
        const glm::vec3& p1 = at(k - 1);
        const glm::vec3& p2 = at(k);
        float len = std::sqrt(dist2(p2, p1));
        if (k == trim_index_) trim_distance_ = along;
        if (trim_index_ < k) {
            along += len;
            continue;
        }
        add_point(p1, along);
        float step = s.segment_length;
        if (s.lod_factor <= -1.1920929e-07f || 1.1920929e-07f <= s.lod_factor)
            step = std::sqrt(dist2(eye, p1)) * s.lod_factor * step;
        if (step <= len) {
            // The tangent handles: toward the neighbours, CurveScale long, when they are a step or more away.
            const glm::vec3& p0 = at(k - 2);
            const glm::vec3& p3 = at(std::min(k + 1, last));
            glm::vec3 a = p1, b = p2;
            if (step * step <= dist2(p0, p1)) a = p1 + glm::normalize(p0 - p1) * s.curve_scale;
            if (step * step <= dist2(p3, p2)) b = p2 + glm::normalize(p3 - p2) * s.curve_scale;
            // 0x59b220.
            place(along, along + len, a, p1, p2, b);
            float frac = step / len, piece = len * frac;
            for (float t = frac; t < 0.99f; t += frac) {
                along += piece;
                add_point(catmull_rom(a, p1, p2, b, t), along);
            }
            along += piece;
        } else {
            place(along, along + len, p1, p1, p2, p2);
            along += len;
        }
    }
    total_ = along;
}

std::optional<glm::vec3> PlayerPath::mover_position(const Mover& m) const {
    if (!m.shown || !m.placed) return std::nullopt;
    return m.position;
}

} // namespace hailstone::game
