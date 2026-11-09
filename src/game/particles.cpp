#include "game/particles.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace hailstone::game {

namespace {

constexpr float kTurn = 2.0f * std::numbers::pi_v<float>;

// Rotates v about the unit axis by a radians (Rodrigues).
glm::vec3 rotate(const glm::vec3& v, const glm::vec3& axis, float a) {
    float c = std::cos(a), sn = std::sin(a);
    return v * c + glm::cross(axis, v) * sn + axis * glm::dot(axis, v) * (1.0f - c);
}

} // namespace

SpellParticleSettings spell_particle_settings(std::optional<float> density, std::optional<float> opacity) {
    SpellParticleSettings s;
    s.density = std::clamp(density.value_or(1.0f), 0.0f, 1.0f);
    s.opacity = std::clamp(opacity.value_or(1.0f), 0.0f, 1.0f);
    return s;
}

SpellFxSource spell_fx_source(std::uint32_t caster, std::uint32_t struck, std::uint32_t me,
                              std::optional<bool> caster_is_npc, std::optional<bool> struck_is_npc) {
    if (me != 0 && (caster == me || struck == me)) return SpellFxSource::Self;
    std::optional<bool> npc = caster_is_npc ? caster_is_npc : struck_is_npc;
    return npc.value_or(false) ? SpellFxSource::Npcs : SpellFxSource::Players;
}

std::vector<SpellEmitterRequest> spawn_stage_emitters(const assets::SpellEffect& effect, int stage,
                                                      std::uint32_t spell_id, const StageActor* caster,
                                                      const StageActor* target, const glm::vec3& point,
                                                      std::int32_t duration_ms, const SpellParticleSettings& settings,
                                                      bool full) {
    std::vector<SpellEmitterRequest> out;
    if (stage < 0 || stage > 2) return out;
    const std::int32_t level = caster ? caster->level : 1;
    for (const auto& slot : effect.stages[static_cast<std::size_t>(stage)].slots) {
        if (slot.emitter < 1 || level < slot.min_level) continue;
        const StageActor* at = nullptr;
        SpellEmitterRequest r;
        r.emitter = slot.emitter;
        r.key = ((caster ? caster->id : 0u) << 16) | (spell_id & 0xffffu);
        r.duration_ms = duration_ms;
        r.density = full ? 1.0f : settings.density;
        r.opacity = full ? 1.0f : settings.opacity;
        switch (slot.location) {
        case 1: at = caster; break;
        case 2: at = nullptr; break; // the missile: not modelled
        case 3: at = target; break;
        default: break;
        }
        if ((slot.location == 1 || slot.location == 3) && !at) continue;
        r.position = at ? at->position : point;
        if (at && slot.attach != 0) {
            // 1 -> point 0; 2..7 -> points 1..6, else point 0; 8 -> no point; 9 / 10 -> points 7 / 8 or the slot
            // is skipped; 11 -> point 9; 12 -> no point, at the chest's x and y (z: GetPlayerFloorHeight 0.1 above
            // the actor, see below), scaled by the actor height. A missing point leaves the actor without one.
            auto existing = [&](int n) { return at->has_point(n) ? n : -1; };
            if (slot.attach == 1) r.bone_point = existing(0);
            else if (slot.attach >= 2 && slot.attach <= 7) {
                r.bone_point = existing(slot.attach - 1);
                if (r.bone_point < 0) r.bone_point = existing(0);
            } else if (slot.attach == 9 || slot.attach == 10) {
                if (!at->has_point(slot.attach - 2)) continue;
                r.bone_point = slot.attach - 2;
            } else if (slot.attach == 11) r.bone_point = existing(9);
            else if (slot.attach == 12) {
                // The floor height is not taken: an emitter on an actor without a point moves to the actor's
                // origin on its first update anyway.
                if (at->has_point(1)) r.position = {at->chest.x, at->chest.y, r.position.z};
                r.scale = at->height;
            } else if (slot.attach != 8) continue; // other ids: no case in the switch, the slot is skipped
            r.actor = at->id;
        }
        out.push_back(r);
    }
    return out;
}

float emitter_duration_seconds(float edd_duration, std::int32_t requested_ms) {
    if (edd_duration < 0.0f) return -edd_duration;
    if (requested_ms > 0) return static_cast<float>(requested_ms) * 0.001f;
    return edd_duration;
}

std::uint32_t emitter_capacity(const assets::EmitterDefinition& d, float duration) {
    if (d.capacity != 0) return std::min<std::uint32_t>(d.capacity, 100000u);
    float life = d.lifetime > 0.0f ? d.lifetime : duration;
    double n = static_cast<double>(d.initial_count) +
               static_cast<double>(d.per_tick) * (static_cast<double>(d.ticks_per_second) *
                                                      static_cast<double>(std::min(life, duration)) + 1.0);
    return static_cast<std::uint32_t>(std::clamp(n, 1.0, 100000.0));
}

glm::mat3 emitter_axes(const glm::mat3& owner, float angle1, float angle2) {
    auto unit = [](glm::vec3 v, glm::vec3 fallback) {
        float len = glm::length(v);
        return std::isfinite(len) && len > 1e-6f ? v / len : fallback;
    };
    glm::vec3 r0 = unit(owner[0], {1, 0, 0}), r1 = unit(owner[1], {0, 1, 0}), r2 = unit(owner[2], {0, 0, 1});
    // Basis-local (transverse 1, transverse 2, axial): rows 0, -1 and 2. An
    // identity owner gives the spell emitters' (+X, -Y, +Z).
    glm::vec3 t1 = r0, t2 = -r1, ax = r2;
    // (t1, t2, axial) = (r0, -r1, r2) is a left-handed set, so the turns are
    // written as world rotations about the axes, signed to give the cases:
    // +0xc8 about transverse 1: -128 (a quarter turn) takes the axis from r2
    // to r1 and transverse 2 from -r1 to r2 (OpenEQ's placement witness:
    // final axes [r1, r0, r2] for axial, radial cosine, radial sine).
    if (angle2 != 0.0f) {
        float a = angle2 * kTurn / 512.0f;
        ax = rotate(ax, t1, a);
        t2 = rotate(t2, t1, a);
    }
    // +0xc4 about transverse 2: 128 takes the axis onto r0, 256 onto -r2.
    if (angle1 != 0.0f) {
        float a = -angle1 * kTurn / 512.0f;
        ax = rotate(ax, t2, a);
        t1 = rotate(t1, t2, a);
    }
    return glm::mat3(t1, t2, ax);
}

float emission_distance_factor(float reduce_distance, std::uint32_t capacity, float distance) {
    float floor = capacity > 0 ? std::min(1.0f, 16.0f / static_cast<float>(capacity)) : 1.0f;
    float near = distance > 0.0f ? std::min(1.0f, reduce_distance / distance) : 1.0f;
    return std::max(near, floor);
}

glm::uvec2 flipbook_grid(std::uint32_t frames) {
    if (frames >= 2 && frames <= 4) return {2, 2};
    if (frames >= 5 && frames <= 8) return {4, 2};
    if (frames >= 9 && frames <= 16) return {4, 4};
    return {1, 1};
}

ParticleSystem::ParticleSystem(std::vector<assets::EmitterDefinition> definitions, std::uint32_t seed)
    : definitions_(std::move(definitions)), rng_(seed) {}

float ParticleSystem::random(float a, float b) {
    return a + (b - a) * std::uniform_real_distribution<float>(0.0f, 1.0f)(rng_);
}

std::int32_t ParticleSystem::create(const SpellEmitterRequest& r) {
    if (r.emitter < 1 || static_cast<std::size_t>(r.emitter) >= definitions_.size()) return 0; // out of range
    if (r.density <= 0.0f) return 0;
    const auto& d = definitions_[static_cast<std::size_t>(r.emitter)];
    Emitter e;
    e.definition = static_cast<std::size_t>(r.emitter);
    e.key = r.key;
    e.actor = r.actor;
    e.point = r.actor != 0 ? r.bone_point : -1;
    e.origin = r.position;
    e.scale = r.scale;
    e.density = r.density;
    e.opacity = r.opacity;
    e.duration = emitter_duration_seconds(d.duration, r.duration_ms);
    e.capacity = emitter_capacity(d, e.duration);
    e.persistent = r.persistent;
    emitters_.push_back(std::move(e));
    return static_cast<std::int32_t>(std::lround(emitters_.back().duration * 1000.0f));
}

bool ParticleSystem::add_fixture(std::size_t definition, const glm::mat4& node, std::uint32_t key) {
    if (definition < 1 || definition >= definitions_.size()) return false;
    const auto& d = definitions_[definition];
    Emitter e;
    e.definition = definition;
    e.key = key;
    e.origin = glm::vec3(node[3]);
    glm::mat3 rot(node);
    e.axes = emitter_axes(rot, d.angle1, d.angle2);
    // The owner scale getter: the first row's length (+0x6c: the actor's scale applies).
    float s = glm::length(rot[0]);
    e.scale = d.actor_scales_size && std::isfinite(s) && s > 0.0f ? s : 1.0f;
    e.duration = emitter_duration_seconds(d.duration, kFixtureDurationMs);
    e.capacity = emitter_capacity(d, e.duration);
    e.persistent = true;
    emitters_.push_back(std::move(e));
    return true;
}

void ParticleSystem::set_persistent_density(float density) {
    for (auto& e : emitters_)
        if (e.persistent) e.density = density;
}

void ParticleSystem::stop(std::uint32_t key) {
    std::erase_if(emitters_, [&](const Emitter& e) { return e.key == key; });
}

void ParticleSystem::move(std::uint32_t key, const glm::vec3& origin) {
    for (auto& e : emitters_)
        if (e.key == key) e.origin = origin;
}

bool ParticleSystem::has(std::uint32_t key) const {
    return std::any_of(emitters_.begin(), emitters_.end(), [&](const Emitter& e) { return e.key == key; });
}

std::size_t ParticleSystem::add_definition(assets::EmitterDefinition d) {
    definitions_.push_back(std::move(d));
    return definitions_.size() - 1;
}

void ParticleSystem::set_still(std::uint32_t key, std::size_t definition, std::vector<Particle> particles) {
    if (definition >= definitions_.size()) return;
    for (auto& e : emitters_)
        if (e.key == key && e.still) {
            e.definition = definition;
            e.particles = std::move(particles);
            return;
        }
    Emitter e;
    e.definition = definition;
    e.key = key;
    e.still = true;
    e.persistent = true;
    e.particles = std::move(particles);
    emitters_.push_back(std::move(e));
}

void ParticleSystem::spawn(Emitter& e, std::uint32_t count) {
    const auto& d = definitions_[e.definition];
    float d0 = d.shape_size.x, d1 = d.shape_size.y != 0.0f ? d.shape_size.y : d.shape_size.x;
    float d2 = d.shape_size.z;
    for (std::uint32_t i = 0; i < count && e.particles.size() < e.capacity; ++i) {
        Particle p;
        float az = random(0.0f, kTurn);
        switch (d.shape) {
        case 1: // ring, evenly spaced
            az = kTurn * static_cast<float>(i) / static_cast<float>(std::max<std::uint32_t>(count, 1));
            p.local = {d0 * std::cos(az), d1 * std::sin(az), 0.0f};
            break;
        case 2:   // sphere in ordered latitude rings: taken as the surface (the ring layout is not traced)
        case 5: { // sphere surface
            float z = random(-1.0f, 1.0f), rr = std::sqrt(std::max(0.0f, 1.0f - z * z));
            p.local = {rr * std::cos(az) * d0, rr * std::sin(az) * d1, z * (d2 != 0.0f ? d2 : d0)};
            break;
        }
        case 3: p.local = {d0 * std::cos(az), d1 * std::sin(az), (random(0.0f, 1.0f) - 0.5f) * d2}; break;
        case 4: { // disk interior, rejection sampled; the pair's angle is the azimuth
            float x, y;
            do {
                x = random(-1.0f, 1.0f);
                y = random(-1.0f, 1.0f);
            } while (x * x + y * y > 1.0f);
            p.local = {x * d0, y * d1, 0.0f};
            if (x != 0.0f || y != 0.0f) az = std::atan2(y, x);
            break;
        }
        case 6: { // box surface
            // EQGraphicsDX9.dll's shape switch, case 6 (@0x100737be): two draws from the signed table (-1..1) for the
            // two free axes, in that order, then the face from the unsigned one (0..1) times 6.0 stored as an integer
            // (rounded to nearest, so 0..6: faces 0 and 6 are half as likely). Faces 0..4 are -x, +x, -y, +y, -z;
            // anything else +z. The original's random numbers come from shuffled tables, this client's from its own
            // generator.
            glm::vec3 dim(d0, d1, d2 != 0.0f ? d2 : d0);
            const float r1 = random(-1.0f, 1.0f);
            const float r2 = random(-1.0f, 1.0f);
            const int face = static_cast<int>(std::nearbyint(random(0.0f, 1.0f) * 6.0f));
            glm::vec3 v;
            switch (face) {
            case 0: v = {-1.0f, r1, r2}; break;
            case 1: v = {1.0f, r1, r2}; break;
            case 2: v = {r1, -1.0f, r2}; break;
            case 3: v = {r1, 1.0f, r2}; break;
            case 4: v = {r1, r2, -1.0f}; break;
            default: v = {r1, r2, 1.0f}; break;
            }
            p.local = v * dim;
            break;
        }
        case 7: { // cone side
            float u = random(0.0f, 1.0f);
            p.local = {d0 * u * std::cos(az), d1 * u * std::sin(az), d2 * u};
            break;
        }
        case 8: { // torus surface
            float phi = random(0.0f, kTurn);
            float ring = d0 + d2 * std::cos(phi);
            p.local = {ring * std::cos(az), ring * std::sin(az), d2 * std::sin(phi)};
            break;
        }
        case 9: p.local = {d0 * std::cos(az), d1 * std::sin(az), 0.0f}; break;
        default: break;
        }
        // A zero-size shape (a point, a disk of radius 0: WLD flame cones)
        // still sprays radially in this direction (OpenEQ docs/WLD_PARTICLE_RUNTIME.md:
        // the azimuth is drawn even with radius 0).
        p.azimuth = az;
        p.local += d.offset;
        p.velocity = {random(d.motion[1].x, d.motion[1].y), random(d.motion[2].x, d.motion[2].y),
                      random(d.motion[0].x, d.motion[0].y)};
        p.radial_speed = random(d.motion[3].x, d.motion[3].y);
        p.orbit_speed = random(d.motion[4].x, d.motion[4].y);
        p.lifetime = d.lifetime > 0.0f ? d.lifetime : std::max(e.duration, 0.001f);
        float wr = random(0.0f, 1.0f);
        p.width = d.width + (d.width_end - d.width) * wr;
        float hr = d.same_random_size ? wr : random(0.0f, 1.0f);
        p.height = d.height_start + (d.height_end - d.height_start) * hr;
        p.rotation = d.random_rotation ? random(0.0f, 512.0f) : 0.0f;
        p.spin = random(d.spin, d.spin2);
        p.anchor = e.origin;
        e.particles.push_back(p);
    }
}

void ParticleSystem::update(float dt, const AttachmentFn& attachment) {
    for (auto& e : emitters_) {
        if (e.still) continue;
        const auto& d = definitions_[e.definition];
        if (e.actor != 0 && attachment)
            if (auto at = attachment(e.actor, e.point)) {
                e.origin = at->position;
                if (e.point >= 0 && d.bone_orientation && at->axes) e.axes = emitter_axes(*at->axes, d.angle1, d.angle2);
            }
        e.age += dt;
        // Emission: the initial count after the delay, then per_tick every
        // 1 / ticks_per_second while the emitter lasts; counts scale with the density.
        auto scaled = [&](std::uint32_t n) {
            return static_cast<std::uint32_t>(std::lround(static_cast<float>(n) * e.density));
        };
        if (!e.started && e.age >= d.delay) {
            e.started = true;
            spawn(e, scaled(d.initial_count));
        } else if (e.started && (e.persistent || e.age <= e.duration + d.delay) && d.ticks_per_second > 0.0f &&
                   d.per_tick > 0) {
            float rate = d.ticks_per_second;
            if (viewer_) rate *= emission_distance_factor(d.reduce_distance, e.capacity, glm::distance(*viewer_, e.origin));
            e.tick_accumulator += dt * rate;
            while (e.tick_accumulator >= 1.0f) {
                e.tick_accumulator -= 1.0f;
                spawn(e, scaled(d.per_tick));
            }
        }
        for (auto& p : e.particles) {
            p.age += dt;
            p.velocity += glm::vec3(d.motion[1].z, d.motion[2].z, d.motion[0].z) * dt;
            p.local += p.velocity * dt;
            p.radial_speed += d.motion[3].z * dt;
            glm::vec2 radial(p.local.x, p.local.y);
            float r = glm::length(radial);
            glm::vec2 dir = r > 1e-5f ? radial / r : glm::vec2(std::cos(p.azimuth), std::sin(p.azimuth));
            float nr = r + p.radial_speed * dt;
            if (nr < 0.0f && !d.negative_radial) nr = 0.0f;
            radial = dir * nr;
            p.orbit_speed += d.motion[4].z * dt;
            float a = p.orbit_speed * dt * kTurn / 512.0f;
            radial = {radial.x * std::cos(a) - radial.y * std::sin(a), radial.x * std::sin(a) + radial.y * std::cos(a)};
            p.local.x = radial.x;
            p.local.y = radial.y;
            p.world_velocity += glm::vec3(d.wind_x, 0.0f, -d.gravity) * dt;
            p.drift += p.world_velocity * dt;
            p.rotation += p.spin * dt;
            glm::vec3 base = d.follow_attachment ? e.origin : p.anchor;
            p.position = base + e.axes * p.local * e.scale + p.drift;
            // Ramps: opacity min(fade in x fade out, max opacity); size grows and shrinks.
            float rem = p.lifetime - p.age;
            float fin = d.fade_in > 0.0f ? std::clamp(p.age / d.fade_in, 0.0f, 1.0f) : 1.0f;
            float fout = d.fade_out > 0.0f ? std::clamp(rem / d.fade_out, 0.0f, 1.0f) : 1.0f;
            float alpha = std::min(fin * fout, d.max_opacity) * e.opacity;
            float grow = d.grow > 0.0f ? std::clamp(p.age / d.grow, 0.0f, 1.0f) : 1.0f;
            float shrink = d.shrink > 0.0f ? std::clamp(rem / d.shrink, 0.0f, 1.0f) : 1.0f;
            // +0x16c multiplies the quad (the update's read at 0x10074aaf); 1
            // in every shipped EDD record, a WLD cloud's size.
            p.size_factor = grow * shrink * e.scale * d.scale1;
            float t = std::clamp(p.age / p.lifetime, 0.0f, 1.0f);
            glm::vec3 c0 = glm::vec3(d.color_start) / 255.0f, c1 = glm::vec3(d.color_end) / 255.0f;
            p.color = glm::vec4(glm::mix(c0, c1, t), alpha);
            if (d.frames > 1) {
                auto grid = flipbook_grid(d.frames);
                std::uint32_t cells = grid.x * grid.y;
                auto f = static_cast<std::int64_t>(std::floor(p.age * d.frames_per_second));
                std::int64_t n = std::min<std::int64_t>(d.frames, cells);
                p.frame = static_cast<std::uint32_t>(((f % n) + n) % n);
            }
        }
        std::erase_if(e.particles, [](const Particle& p) { return p.age >= p.lifetime; });
    }
    std::erase_if(emitters_, [&](const Emitter& e) {
        return !e.persistent && e.started && e.age > e.duration + definitions_[e.definition].delay &&
               e.particles.empty();
    });
}

} // namespace hailstone::game
