#include "game/precipitation.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>

#include "core/game_dir.h"
#include "core/ini_file.h"

namespace hailstone::game {

namespace {

constexpr float kRandomScale = 3.051851e-05f; // 1 / 32767 (0x101345f0)
constexpr float kFrom = -1200.0f;             // the ground probe starts 1200 units up the wind (0x1013acc0)
constexpr float kProbe = 1700.0f;             // and runs 1700 units along it (0x1013acbc)
constexpr float kAboveGround = 0.05f;         // the landing point's lift (0x10132e6c)
constexpr float kFlatEnough = 0.8f;           // a splash needs ground whose normal z is above this (0x10132e70)

float number(const IniFile& ini, const std::string& section, std::string_view key, float fallback) {
    auto v = ini.get(section, key);
    if (!v || v->empty()) return fallback;
    return std::strtof(v->c_str(), nullptr);
}

std::int32_t integer(const IniFile& ini, const std::string& section, std::string_view key, std::int32_t fallback) {
    auto v = ini.get(section, key);
    if (!v || v->empty()) return fallback;
    return static_cast<std::int32_t>(std::strtol(v->c_str(), nullptr, 10));
}

float unit(const PrecipitationField::Frame& f) { return static_cast<float>(f.random()) * kRandomScale; }

} // namespace

PrecipitationDefinitions PrecipitationDefinitions::parse(const IniFile& ini) {
    PrecipitationDefinitions out;
    for (std::size_t k = 0; k < kPrecipitationKindNames.size(); ++k) {
        auto& kind = out.kinds[k];
        const std::string name(kPrecipitationKindNames[k]);
        for (int n = 1; n <= 3; ++n) {
            std::int32_t i = integer(ini, "Settings", name + "Falling" + std::to_string(n), -1);
            if (i != -1) {
                const std::string s = "Falling" + std::to_string(i);
                PrecipitationFalling d;
                d.max_speed = number(ini, s, "MaxSpeed", d.max_speed);
                d.acceleration = number(ini, s, "Acceleration", d.acceleration);
                d.width = number(ini, s, "Width", d.width);
                d.height = number(ini, s, "Height", d.height);
                d.rate = number(ini, s, "Rate", d.rate);
                d.frames = integer(ini, s, "Frames", 0);
                d.orientation = integer(ini, s, "Orientation", 0);
                d.file = ini.get_string(s, "File");
                kind.falling.push_back(std::move(d));
            }
            i = integer(ini, "Settings", name + "Splashing" + std::to_string(n), -1);
            if (i != -1) {
                const std::string s = "Splashing" + std::to_string(i);
                PrecipitationSplash d;
                d.life = number(ini, s, "Life", d.life);
                d.chance = number(ini, s, "Chance", d.chance);
                d.width = number(ini, s, "Width", d.width);
                d.height = number(ini, s, "Height", d.height);
                d.rate = number(ini, s, "Rate", d.rate);
                d.frames = integer(ini, s, "Frames", 0);
                d.orientation = integer(ini, s, "Orientation", 0);
                d.file = ini.get_string(s, "File");
                kind.splashing.push_back(std::move(d));
            }
        }
    }
    return out;
}

std::optional<PrecipitationDefinitions> PrecipitationDefinitions::load(const GameDir& game, std::string_view name) {
    IniFile ini;
    auto read = [&](std::string_view file) {
        auto path = game.find("Resources/Precipitation/" + std::string(file) + ".ini");
        return path && ini.load(*path);
    };
    bool ok = read(name) && integer(ini, "Settings", "Valid", 0) == 1;
    if (!ok) {
        ini = IniFile{};
        if (!read("default")) return std::nullopt;
    }
    return parse(ini);
}

std::uint32_t blend_precipitation_colour(std::uint32_t a, std::uint32_t b, float t) {
    std::uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        float x = static_cast<float>((a >> shift) & 0xff) * (1.0f - t) + static_cast<float>((b >> shift) & 0xff) * t;
        auto v = static_cast<std::int32_t>(std::nearbyint(x)); // stored as an integer, rounded to nearest
        out |= static_cast<std::uint32_t>(std::clamp(v, 0, 255)) << shift;
    }
    return out;
}

PrecipitationField::PrecipitationField(std::uint32_t count) : particles_(count) {
    for (std::uint32_t i = 0; i < count; ++i) free_.push_back(i);
}

void PrecipitationField::set_kind(std::int32_t kind) {
    if (kind == kind_) return;
    kind_ = kind;
    if (kind == 0xff) stop(true, true);
}

void PrecipitationField::set_wind(const glm::vec3& wind) {
    float len = glm::length(wind);
    wind_ = len > 0.0f ? wind / len : wind;
    wind_changed_ = true;
    stop(false, true);
}

void PrecipitationField::stop(bool active, bool cached) {
    if (active) {
        for (std::uint32_t i : active_) particles_[i].state = PrecipitationParticle::Dead;
        free_.insert(free_.end(), active_.begin(), active_.end());
        active_.clear();
    }
    if (cached) {
        free_.insert(free_.end(), cached_.begin(), cached_.end());
        cached_.clear();
    }
}

void PrecipitationField::pick_definitions(const Frame& f, std::int32_t& falling, std::int32_t& splash,
                                          float& life) const {
    const auto* defs = defs_.kind(kind_);
    falling = -1;
    splash = -1;
    life = 0.0f;
    if (!defs) return;
    if (!defs->splashing.empty()) {
        splash = f.random() % static_cast<std::int32_t>(defs->splashing.size());
        const auto& s = defs->splashing[static_cast<std::size_t>(splash)];
        life = s.life;
        if (s.chance < unit(f)) life = 0.0f; // splashes Chance of the time
    }
    if (!defs->falling.empty()) falling = f.random() % static_cast<std::int32_t>(defs->falling.size());
}

void PrecipitationField::launch(PrecipitationParticle& p, const glm::vec3& at, float speed, float acceleration,
                                float max_speed, float life, std::int32_t splash, std::int32_t falling,
                                const Frame& f) {
    p.stale = false;
    p.position = at;
    p.splash = splash;
    p.state = PrecipitationParticle::Falling;
    p.speed = speed;
    p.falling = falling;
    p.start_speed = speed;
    p.max_speed = max_speed;
    p.acceleration = acceleration;
    // The ground: CollideLineSegment from 1200 units up the wind to 500 down it; the clamp point stops 0.001 short.
    glm::vec3 from = at + wind_ * kFrom, delta = wind_ * kProbe;
    std::optional<PrecipitationHit> hit = f.ground ? f.ground(from, from + delta, false) : std::nullopt;
    // Not the original's: a back face below the particle stops it too, so a roof that is only a ceiling seen from
    // above (the East Commonlands tunnel's, under a mountain with no top) keeps the rain off the floor under it. Only
    // below it: a zone's ceiling (East Commonlands' at z 719, facing down) would be every drop's ground otherwise,
    // above the drop, which then lands as it starts and none is ever seen.
    if (f.ground) {
        const float start = -kFrom / kProbe; // the particle's place along the probe
        if (auto under = f.ground(at, from + delta, true)) {
            const float t = start + under->t * (1.0f - start);
            if (!hit || t < hit->t) hit = PrecipitationHit{t, under->normal_z};
        }
    }
    float t = hit ? hit->t : 1.0f;
    if (hit && !(hit->normal_z > kFlatEnough)) p.may_splash = false;
    glm::vec3 v = delta * t;
    float len = glm::length(v);
    p.ground = len > 0.001f ? from + v * ((len - 0.001f) / len) : from;
    p.ground.z += kAboveGround;
    p.splash_life = life;
}

bool PrecipitationField::spawn_new(const Frame& f) {
    std::uint32_t index;
    if (!free_.empty()) {
        index = free_.front();
        free_.pop_front();
    } else if (!cached_.empty()) {
        index = cached_.front();
        cached_.pop_front();
    } else {
        return false;
    }
    active_.push_back(index);
    PrecipitationParticle& p = particles_[index];

    // Round the camera, mostly ahead of it: 3/4 of the turns within 128/512 of its heading, the rest out to 256/512.
    const float near = unit(f);
    float a = (unit(f) - 0.5f) * 2.0f;
    float turn;
    if (a > 0.75f)
        turn = ((a - 0.75f) * 4.0f + 1.0f) * 128.0f;
    else if (a < -0.75f)
        turn = (a + 0.75f) * 4.0f * 128.0f - 128.0f;
    else
        turn = a * 128.0f;
    float heading = turn + f.heading;
    if (heading > 512.0f)
        heading -= 512.0f;
    else if (heading < 0.0f)
        heading += 512.0f;
    const float rise = unit(f);
    const float height = near * 100.0f * rise + 40.0f;       // 40 to 140 above the camera
    const float distance = near * 200.0f;                    // up to 200 away, more of them near
    const float slow = (1.0f - near) * (1.0f - near);        // the near ones start faster
    const float radians = heading * (2.0f * std::numbers::pi_v<float> / 512.0f);
    glm::vec3 at(f.camera.x + std::cos(radians) * distance, f.camera.y + std::sin(radians) * distance,
                 f.camera.z + height);

    std::int32_t falling, splash;
    float life;
    pick_definitions(f, falling, splash, life);
    const auto* defs = defs_.kind(kind_);
    PrecipitationFalling def;
    if (defs && falling >= 0) def = defs->falling[static_cast<std::size_t>(falling)];
    float speed = falling >= 0 ? def.max_speed * slow : slow;
    launch(p, at, speed, def.acceleration, def.max_speed, life, splash, falling, f);

    // Its colour: a random blend of the sky's two precipitation colours.
    p.colour = blend_precipitation_colour(f.colours[0], f.colours[1], unit(f));
    return true;
}

bool PrecipitationField::spawn_column(const Frame& f) {
    if (cached_.empty()) return false;
    const float rise = unit(f);
    std::uint32_t index = cached_.front();
    cached_.pop_front();
    active_.push_back(index);
    PrecipitationParticle& p = particles_[index];
    std::int32_t falling, splash;
    float life;
    pick_definitions(f, falling, splash, life);
    const auto* defs = defs_.kind(kind_);
    PrecipitationFalling def;
    if (defs && falling >= 0) def = defs->falling[static_cast<std::size_t>(falling)];
    // 0x1007f7f0: 40 to 140 units above its own ground, at its first speed, keeping its colour.
    const float height = rise * 100.0f + 40.0f;
    p.speed = p.start_speed;
    p.state = PrecipitationParticle::Falling;
    p.max_speed = def.max_speed;
    p.splash = splash;
    p.falling = falling;
    p.acceleration = def.acceleration;
    p.splash_life = life;
    p.position = p.ground - wind_ * height;
    return true;
}

void PrecipitationField::update(const Frame& f) {
    // round(count x dt x p): at most 10 new particles, the rest on landed columns.
    auto emit = [&](float seconds) {
        float share = static_cast<float>(particles_.size()) * seconds * f.intensity;
        auto n = static_cast<std::int32_t>(share >= 0.0f ? std::floor(share + 0.5f) : std::ceil(share - 0.5f));
        std::int32_t fresh = kNewPerUpdate;
        if (kind_ == 0xff)
            n = 0;
        if (n <= kNewPerUpdate - 1) fresh = n;
        for (std::int32_t i = 0; i < fresh; ++i) {
            if (!spawn_new(f)) break;
            --n;
        }
        for (std::int32_t i = 0; i < n; ++i)
            if (!spawn_column(f)) break;
    };
    const bool clocked = f.motion_seconds >= 0.0f;
    const float dt = clocked ? f.motion_seconds : f.seconds;
    if (clocked)
        for (float s : f.spawn_steps) emit(s);
    else
        emit(dt);

    for (std::uint32_t index : active_) {
        PrecipitationParticle& p = particles_[index];
        if (p.state == PrecipitationParticle::Dead) continue;
        p.age += dt;
        p.speed += p.acceleration * dt;
        if (p.max_speed < p.speed) p.speed = p.max_speed;
        if (p.state == PrecipitationParticle::Falling) {
            p.position += wind_ * (p.speed * dt);
            if (p.position.z <= p.ground.z) {
                p.state = p.splash_life > 0.0f && p.may_splash ? PrecipitationParticle::Splashing
                                                               : PrecipitationParticle::Dead;
                p.age = 0.0f;
                p.position = p.ground;
            }
        } else if (p.state == PrecipitationParticle::Splashing) {
            p.splash_life -= dt;
            if (p.splash_life <= 0.0f) p.state = PrecipitationParticle::Dead;
        } else {
            p.state = PrecipitationParticle::Dead;
        }
        p.stale = p.stale || wind_changed_;
    }

    // 0x10080f10: the dead ones go to the landed columns, or with a stale ground to the free list; columns more
    // than 200 units from the camera are let go.
    still_.clear();
    for (std::uint32_t index : active_) {
        const PrecipitationParticle& p = particles_[index];
        if (p.state != PrecipitationParticle::Dead)
            still_.push_back(index);
        else if (p.stale)
            free_.push_back(index);
        else
            cached_.push_back(index);
    }
    active_.swap(still_);
    std::erase_if(cached_, [&](std::uint32_t index) {
        glm::vec3 d = particles_[index].position - f.camera;
        if (glm::dot(d, d) <= kColumnReach * kColumnReach) return false;
        free_.push_back(index);
        return true;
    });
    wind_changed_ = false;
}

} // namespace hailstone::game
