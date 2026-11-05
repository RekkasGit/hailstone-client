#include "game/spell_projectiles.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>

#include "game/spells.h"

namespace nmsc::game {

namespace {

constexpr int kSpaCurrentHp = 0;      // SE_CurrentHP
constexpr int kSpaCurrentHpOnce = 79; // SE_CurrentHPOnce

bool has_word(const std::string& name, std::initializer_list<const char*> words) {
    for (const char* w : words)
        if (name.find(w) != std::string::npos) return true;
    return false;
}

// A number in [0, 1) from two others: the sparks' scatter (the same projectile scatters the same way).
float hash(float a, float b) {
    float h = std::sin(a * 12.9898f + b * 78.233f) * 43758.5453f;
    return h - std::floor(h);
}

glm::vec3 scatter(float seed, float i) {
    float u = hash(seed, i) * 2.0f - 1.0f;
    float a = hash(seed + 3.1f, i) * 6.2831853f;
    float r = std::sqrt(std::max(0.0f, 1.0f - u * u));
    return {r * std::cos(a), r * std::sin(a), u};
}

// How each element's sparks go: their speed, the pull on them (up for embers, down for shards and drips), their
// life, size, and how many a second a flying projectile sheds.
struct SparkKind {
    float speed, pull, life, size, rate;
};

SparkKind spark_kind(SpellElement e) {
    switch (e) {
    case SpellElement::Fire: return {7.0f, 6.0f, 0.7f, 0.28f, 70.0f};
    case SpellElement::Cold: return {6.0f, -32.0f, 0.6f, 0.22f, 50.0f};
    case SpellElement::Lightning: return {20.0f, 0.0f, 0.18f, 0.14f, 90.0f};
    case SpellElement::Poison:
    case SpellElement::Disease: return {2.5f, -26.0f, 0.8f, 0.24f, 40.0f};
    case SpellElement::Corruption: return {3.5f, 3.0f, 0.8f, 0.26f, 45.0f};
    case SpellElement::Earth: return {5.0f, -40.0f, 0.7f, 0.22f, 45.0f};
    default: return {4.5f, 0.0f, 0.7f, 0.2f, 55.0f};
    }
}

bool done(const Projectile& p, double now) {
    double age = now - p.phase_at;
    switch (p.phase) {
    case Projectile::Phase::Burst: return age > (p.style.form == ProjectileForm::Bolt ? SpellProjectiles::kBurst
                                                                                      : SpellProjectiles::kCometBurst);
    case Projectile::Phase::Fizzle: return age > SpellProjectiles::kFizzle;
    default: return false;
    }
}

} // namespace

ProjectileStyle projectile_style(const SpellData& sp) {
    ProjectileStyle s;
    if (sp.beneficial() || sp.duration_cap != 0 || sp.duration_formula != 0) return s;
    bool damage = false;
    for (std::size_t i = 0; i < sp.effect.size(); ++i)
        if ((sp.effect[i] == kSpaCurrentHp || sp.effect[i] == kSpaCurrentHpOnce) && sp.base[i] < 0) damage = true;
    if (!damage) return s;
    std::string name = sp.name;
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    bool falls = has_word(name, {"comet", "meteor", "star", "hail", "falling", "sky"});
    switch (sp.target_type) {
    case kTargetOptional:
    case kTargetSingle:
    case kTargetAnimal:
    case kTargetUndead:
    case kTargetSummoned:
        s.form = falls ? ProjectileForm::Comet : ProjectileForm::Bolt;
        s.server_missile = sp.target_type == kTargetOptional;
        break;
    case kTargetAETarget:
        s.form = ProjectileForm::Rain;
        s.area = std::max(sp.ae_range, 10.0f);
        break;
    default: return s;
    }
    switch (sp.resist) {
    case 2: s.element = SpellElement::Fire; break;
    case 3: s.element = SpellElement::Cold; break;
    case 4: s.element = SpellElement::Poison; break;
    case 5: s.element = SpellElement::Disease; break;
    case 6:
    case 7: s.element = SpellElement::Chromatic; break;
    case 8: s.element = SpellElement::Earth; break;
    case 9: s.element = SpellElement::Corruption; break;
    default: s.element = SpellElement::Magic; break;
    }
    if (has_word(name, {"lightning", "thunder", "electr", "volt", "jolt", "static", "spark"}))
        s.element = SpellElement::Lightning;
    return s;
}

glm::vec3 element_light(SpellElement e) {
    switch (e) {
    case SpellElement::Fire: return {1.0f, 0.45f, 0.12f};
    case SpellElement::Cold: return {0.4f, 0.7f, 1.0f};
    case SpellElement::Lightning: return {0.6f, 0.75f, 1.0f};
    case SpellElement::Poison: return {0.35f, 1.0f, 0.2f};
    case SpellElement::Disease: return {0.8f, 0.75f, 0.2f};
    case SpellElement::Corruption: return {0.8f, 0.08f, 0.18f};
    case SpellElement::Chromatic: return {0.9f, 0.6f, 1.0f};
    case SpellElement::Earth: return {0.9f, 0.6f, 0.3f};
    default: return {0.7f, 0.35f, 1.0f};
    }
}

Projectile& SpellProjectiles::add(std::uint32_t spell, const ProjectileStyle& style, std::uint32_t caster,
                                  std::uint32_t target, double now) {
    Projectile p;
    p.spell = spell;
    p.caster = caster;
    p.target = target;
    p.style = style;
    p.phase_at = now;
    p.seed = static_cast<float>(++counter_ % 997) * 1.618f;
    p.size = style.form == ProjectileForm::Bolt ? 1.0f : 2.2f;
    projectiles_.push_back(p);
    return projectiles_.back();
}

void SpellProjectiles::cast_begun(std::uint32_t spell, const ProjectileStyle& style, std::uint32_t caster,
                                  double now) {
    last_cast_[caster] = {spell, style, now};
}

void SpellProjectiles::own_cast(std::uint32_t spell, const ProjectileStyle& style, std::uint32_t caster,
                                std::uint32_t target, double now, double cast_seconds) {
    if (style.form != ProjectileForm::Bolt && style.form != ProjectileForm::Comet) return;
    if (style.server_missile || target == 0 || target == caster) return;
    own_cast_ended(caster, now); // a new cast replaces one still waiting
    Projectile& p = add(spell, style, caster, target, now);
    p.arrive_at = now + std::max(cast_seconds, 0.0);
}

void SpellProjectiles::own_cast_ended(std::uint32_t caster, double now) {
    // Leaving well before the cast's end means it did not end as planned.
    std::erase_if(projectiles_, [&](const Projectile& p) {
        return p.caster == caster && p.phase == Projectile::Phase::Waiting && !p.landed && now < p.arrive_at - 0.25;
    });
}

double SpellProjectiles::landed(std::uint32_t spell, const ProjectileStyle& style, std::uint32_t source,
                                std::uint32_t target, double now, float distance) {
    if (style.form == ProjectileForm::None) return 0.0;
    for (Projectile& p : projectiles_) {
        if (p.landed || p.spell != spell || p.caster != source || p.target != target) continue;
        if (p.phase == Projectile::Phase::Burst || p.phase == Projectile::Phase::Fizzle) continue;
        p.landed = true;
        if (p.phase == Projectile::Phase::Holding) {
            burst(p, now);
            return 0.0;
        }
        if (p.phase == Projectile::Phase::Waiting) {
            // The cast ended sooner than planned: a quick one.
            p.arrive_at = now + kMinFlight;
            return kMinFlight;
        }
        return p.missile ? 0.0 : std::max(0.0, p.arrive_at - now);
    }
    if (source == target || source == 0) return 0.0;
    // Another's spell (or a proc): its projectile leaves now.
    if (style.form == ProjectileForm::Rain) {
        // One comet on the target, two more round it.
        for (int i = 0; i < 3; ++i) {
            Projectile& p = add(spell, style, source, target, now);
            p.landed = true;
            p.size = i == 0 ? 1.7f : 1.1f;
            p.arrive_at = now + kCometFall + 0.12 * i;
            if (i > 0) {
                float a = hash(p.seed, 1.0f) * 6.2831853f;
                float r = style.area * (0.3f + 0.4f * hash(p.seed, 2.0f));
                p.offset = glm::vec3(std::cos(a) * r, std::sin(a) * r, 0.0f);
            }
        }
        return kCometFall;
    }
    if (style.server_missile) return 0.0; // its missile shows its flight
    Projectile& p = add(spell, style, source, target, now);
    p.landed = true;
    double wait = style.form == ProjectileForm::Comet ? kCometFall : bolt_flight(style, distance);
    p.arrive_at = now + wait;
    return wait;
}

double SpellProjectiles::bolt_flight(const ProjectileStyle& style, float distance) {
    if (style.element == SpellElement::Lightning) return kMinFlight;
    return std::clamp(static_cast<double>(distance / kBoltSpeed), kMinFlight, kMaxFlight);
}

bool SpellProjectiles::follow_missile(std::uint32_t serial, std::uint32_t source, std::uint32_t target, double now) {
    auto it = last_cast_.find(source);
    if (it == last_cast_.end() || now - it->second.at > 30.0 || !it->second.style.server_missile) return false;
    Projectile& p = add(it->second.spell, it->second.style, source, target, now);
    p.missile = serial;
    p.phase = Projectile::Phase::Flying;
    p.launch_at = now;
    p.arrive_at = now + 10.0; // until the missile ends
    return true;
}

bool SpellProjectiles::follows(std::uint32_t serial) const {
    return std::any_of(projectiles_.begin(), projectiles_.end(),
                       [&](const Projectile& p) { return p.missile == serial && p.phase == Projectile::Phase::Flying; });
}

void SpellProjectiles::launch(Projectile& p, double now, double flight_seconds) {
    p.phase = Projectile::Phase::Flying;
    p.phase_at = p.launch_at = now;
    p.arrive_at = now + flight_seconds;
    p.head = path_point(p, 0.0f);
    p.trail.clear();
    p.next_spark = now;
}

void SpellProjectiles::burst(Projectile& p, double now) {
    p.phase = Projectile::Phase::Burst;
    p.phase_at = now;
    int n = p.style.form == ProjectileForm::Bolt ? 28 : 48;
    for (int i = 0; i < n; ++i) spark(p, now, i, true);
}

void SpellProjectiles::spark(const Projectile& p, double now, int index, bool is_burst) {
    SparkKind k = spark_kind(p.style.element);
    ProjectileSpark s;
    s.element = p.style.element;
    float fi = static_cast<float>(index) + static_cast<float>(std::fmod(now, 97.0));
    glm::vec3 dir = scatter(p.seed, fi);
    float speed = k.speed * (0.4f + hash(p.seed + 7.0f, fi));
    if (is_burst) {
        speed = (p.style.form == ProjectileForm::Bolt ? 22.0f : 34.0f) * (0.5f + hash(p.seed + 5.0f, fi)) * p.size * 0.7f;
        if (p.style.form != ProjectileForm::Bolt) dir.z = std::abs(dir.z) * 0.6f + 0.2f; // thrown up off the ground
    }
    s.position = p.head + dir * (0.3f * p.size);
    s.velocity = dir * speed + (is_burst ? glm::vec3(0.0f) : p.velocity * 0.15f);
    s.born = now;
    s.life = k.life * (0.6f + 0.8f * hash(p.seed + 9.0f, fi)) * (is_burst ? 1.4f : 1.0f);
    s.size = k.size * (0.7f + 0.6f * hash(p.seed + 11.0f, fi)) * (is_burst ? 1.3f : 1.0f) * std::sqrt(p.size);
    sparks_.push_back(s);
}

glm::vec3 SpellProjectiles::path_point(const Projectile& p, float s) {
    s = std::clamp(s, 0.0f, 1.0f);
    if (p.style.form == ProjectileForm::Bolt) {
        glm::vec3 d = p.aim - p.origin;
        float len = glm::length(d);
        float arc = std::min(len * 0.1f, 8.0f);
        // A little to one side as well, by the seed: bolts thrown together do not overlap.
        glm::vec3 side = len > 1e-3f ? glm::normalize(glm::cross(d, glm::vec3(0, 0, 1)) + glm::vec3(0, 0, 1e-4f))
                                     : glm::vec3(0.0f);
        float sway = (hash(p.seed, 0.5f) - 0.5f) * arc;
        float bow = std::sin(s * 3.14159265f);
        return p.origin + d * s + glm::vec3(0, 0, arc * bow) + side * (sway * bow);
    }
    // A comet: down from the sky, quickening.
    float e = s * s * (0.6f + 0.4f * s);
    return glm::mix(p.aim + p.sky, p.aim, e);
}

float SpellProjectiles::flight(const Projectile& p, double now) {
    double span = p.arrive_at - p.launch_at;
    if (span <= 1e-6) return 1.0f;
    return static_cast<float>(std::clamp((now - p.launch_at) / span, 0.0, 1.0));
}

void SpellProjectiles::update(double now, const ProjectileWorld& world) {
    for (Projectile& p : projectiles_) {
        bool comet = p.style.form != ProjectileForm::Bolt;
        // Where it is going.
        if (p.target) {
            if (comet) {
                if (auto f = world.feet ? world.feet(p.target) : std::nullopt) p.aim = *f + p.offset;
            } else if (auto c = world.chest ? world.chest(p.target) : std::nullopt) {
                p.aim = *c;
            }
        }
        switch (p.phase) {
        case Projectile::Phase::Waiting: {
            std::optional<glm::vec3> from = world.hands ? world.hands(p.caster, p.aim) : std::nullopt;
            if (comet) {
                // Out of the sky behind the caster, so it is seen coming.
                double fall = kCometFall;
                if (now >= p.arrive_at - fall || p.arrive_at <= now) {
                    glm::vec3 back = from ? *from - p.aim : glm::vec3(1, 0, 0);
                    back.z = 0.0f;
                    float bl = glm::length(back);
                    back = bl > 1e-3f ? back / bl : glm::vec3(1, 0, 0);
                    float h = 90.0f * std::sqrt(p.size);
                    float turn = (hash(p.seed, 4.0f) - 0.5f) * 0.8f;
                    glm::vec3 side(-back.y, back.x, 0.0f);
                    p.sky = (back + side * turn) * (h * 0.5f) + glm::vec3(0, 0, h);
                    launch(p, now, std::max(p.arrive_at - now, 0.15));
                }
                break;
            }
            if (!from) break;
            p.origin = *from;
            double flight_s = bolt_flight(p.style, glm::length(p.aim - p.origin));
            if (now >= p.arrive_at - flight_s) launch(p, now, std::max(p.arrive_at - now, 0.1));
            break;
        }
        case Projectile::Phase::Flying: {
            glm::vec3 before = p.head;
            if (p.missile) {
                auto m = world.missile ? world.missile(p.missile) : std::nullopt;
                if (!m) {
                    burst(p, now);
                    break;
                }
                p.head = *m;
            } else {
                float s = flight(p, now);
                p.head = path_point(p, s);
                if (s >= 1.0f) {
                    if (p.landed) burst(p, now);
                    else {
                        p.phase = Projectile::Phase::Holding;
                        p.phase_at = now;
                    }
                }
            }
            if (!p.trail.empty()) {
                double dt = now - p.trail.back().time;
                if (dt > 1e-4) p.velocity = (p.head - before) / static_cast<float>(dt);
            }
            break;
        }
        case Projectile::Phase::Holding:
            p.head = p.aim;
            if (now - p.phase_at > kHold) {
                p.phase = Projectile::Phase::Fizzle;
                p.phase_at = now;
            }
            break;
        default: break;
        }
        if (p.phase == Projectile::Phase::Flying || p.phase == Projectile::Phase::Holding) {
            p.trail.push_back({p.head, now});
            while (p.trail.size() > 2 && now - p.trail.front().time > kTrail) p.trail.erase(p.trail.begin());
            // Sparks, at the element's rate (half as many while it waits).
            float rate = spark_kind(p.style.element).rate * (p.phase == Projectile::Phase::Holding ? 0.5f : 1.0f) *
                         std::sqrt(p.size);
            int count = 0;
            while (p.next_spark <= now && count < 16) {
                spark(p, now, static_cast<int>(counter_++ % 4096), false);
                p.next_spark += 1.0 / rate;
                ++count;
            }
            if (p.next_spark < now) p.next_spark = now;
        } else if (p.phase == Projectile::Phase::Burst || p.phase == Projectile::Phase::Fizzle) {
            if (!p.trail.empty() && now - p.trail.front().time > kTrail) p.trail.erase(p.trail.begin());
        }
    }
    std::erase_if(projectiles_, [&](const Projectile& p) { return done(p, now); });
    // The sparks fly, pulled by their element, and slow in the air.
    float dt = static_cast<float>(std::clamp(now - last_update_.value_or(now), 0.0, 0.1));
    last_update_ = now;
    for (ProjectileSpark& s : sparks_) {
        float pull = spark_kind(s.element).pull;
        s.velocity.z += pull * dt;
        s.velocity *= std::exp(-1.6f * dt);
        s.position += s.velocity * dt;
    }
    std::erase_if(sparks_, [&](const ProjectileSpark& s) { return now - s.born > s.life; });
    for (auto it = last_cast_.begin(); it != last_cast_.end();)
        it = now - it->second.at > 60.0 ? last_cast_.erase(it) : std::next(it);
}

void SpellProjectiles::clear() {
    projectiles_.clear();
    sparks_.clear();
    last_cast_.clear();
}

} // namespace nmsc::game
