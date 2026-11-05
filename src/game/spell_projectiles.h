#pragma once

// The modern lighting's spell projectiles (docs/graphics.md, "Spell bolts and comets"): a single-target nuke flies
// from the caster's hands to the target as a bolt of its element, a comet or meteor falls on its target out of the
// sky, and a rain drops comets round each target it hits. Not the original's: there, most of these spells have no
// projectile at all (the cast stage plays on the caster, the impact stage on the target when OP_Action 231 lands
// it), and the mechanics stay its: the damage, the messages and the landing are the server's, when it sends them.
// The projectile only times itself to them:
//   - the player's own cast is known from its start (OP_BeginCast, with the target the cast was aimed at), so its
//     projectile leaves in time to arrive as the cast ends, and bursts when the landing comes (it waits a little at
//     the target for it, and fizzles when it does not come: an interrupted or resisted-away cast);
//   - another's cast names no target until it lands, so its projectile leaves then, and the spell's impact
//     particles wait for it to arrive (landed()'s answer);
//   - a spell the server flies itself (target type 1, the magician's bolts: Mob::TrySpellProjectile, a missile
//     packet; game/missiles.h) follows that missile, and bursts where it ends.
// The sparks, embers and shards they shed are simulated here too; render::SpellFxRenderer draws them all.

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace nmsc::game {

struct SpellData;

// What the projectile is made of, from the spell's resist type (and its name, for lightning).
enum class SpellElement : std::uint8_t { Fire, Cold, Magic, Lightning, Poison, Disease, Corruption, Chromatic, Earth };

enum class ProjectileForm : std::uint8_t {
    None,
    Bolt,  // flies from the caster to the target
    Comet, // falls on the target from the sky
    Rain,  // comets round each target an area spell hits
};

struct ProjectileStyle {
    ProjectileForm form = ProjectileForm::None;
    SpellElement element = SpellElement::Magic;
    bool server_missile = false; // the server flies it (target type 1): follow its missile
    float area = 0.0f;           // a rain's reach (the spell's AE range)
};

// Detrimental direct damage (a current-HP effect below zero, no duration) on one target is a bolt, or a comet when
// its name says it falls (comet, meteor, star, hail...); on an area round a target (target type 8) it is a rain.
// Everything else (buffs, damage over time, lifetaps, point-blank areas) has none.
ProjectileStyle projectile_style(const SpellData& spell);

// An element's light: the colour its projectile lights the scene with (linear, about 1 at its brightest channel).
glm::vec3 element_light(SpellElement e);

struct Projectile {
    enum class Phase : std::uint8_t {
        Waiting, // the player's own cast: to leave in time to arrive as the cast ends
        Flying,
        Holding, // arrived before the landing: waits at the target for it
        Burst,
        Fizzle, // the landing never came
    };
    std::uint32_t spell = 0;
    std::uint32_t caster = 0, target = 0;
    ProjectileStyle style;
    Phase phase = Phase::Waiting;
    double arrive_at = 0.0;  // when it reaches the target (planned)
    double launch_at = 0.0;  // when it left
    double phase_at = 0.0;   // when the phase began
    bool landed = false;     // the server has landed the spell
    std::uint32_t missile = 0; // the server missile it follows (MissileList serial), 0 none
    glm::vec3 origin{0.0f};  // where it left from
    glm::vec3 sky{0.0f};     // a comet's start, from where it lands
    glm::vec3 offset{0.0f};  // where a comet lands, from its target's foot (a rain's comets round it)
    glm::vec3 aim{0.0f};     // where it is going (the target's chest, a comet's foot), as last seen
    glm::vec3 head{0.0f};
    glm::vec3 velocity{0.0f};
    float size = 1.0f;       // scale: 1 a bolt
    float seed = 0.0f;
    // The head's recent path (its trail), newest last.
    struct Sample {
        glm::vec3 position;
        double time;
    };
    std::vector<Sample> trail;
    double next_spark = 0.0;
};

// A spark, ember or shard thrown off a projectile.
struct ProjectileSpark {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    double born = 0.0;
    float life = 0.5f;
    float size = 0.15f;
    SpellElement element = SpellElement::Fire;
};

// Where the projectiles' ends are, asked each update.
struct ProjectileWorld {
    // A caster's casting point, its hands held out towards `toward`.
    std::function<std::optional<glm::vec3>(std::uint32_t id, const glm::vec3& toward)> hands;
    std::function<std::optional<glm::vec3>(std::uint32_t id)> chest;  // a target's middle
    std::function<std::optional<glm::vec3>(std::uint32_t id)> feet;   // a target's foot (a comet's landing)
    std::function<std::optional<glm::vec3>(std::uint32_t serial)> missile; // a server missile's place, while it flies
};

class SpellProjectiles {
public:
    static constexpr float kBoltSpeed = 160.0f;    // units a second
    static constexpr double kMinFlight = 0.18, kMaxFlight = 0.55;
    static constexpr double kCometFall = 0.7;
    static constexpr double kHold = 0.7;           // how long an early arrival waits for its landing
    static constexpr double kBurst = 0.75, kCometBurst = 1.1, kFizzle = 0.35;
    static constexpr double kTrail = 0.16;         // seconds of path the trail keeps

    // Any cast's start (OP_BeginCast): remembered for a server missile of it.
    void cast_begun(std::uint32_t spell, const ProjectileStyle& style, std::uint32_t caster, double now);
    // The player's own cast at `target`: its projectile arrives `cast_seconds` from now.
    void own_cast(std::uint32_t spell, const ProjectileStyle& style, std::uint32_t caster, std::uint32_t target,
                  double now, double cast_seconds);
    // The player's cast ended without its spell landing yet (interrupted, fizzled): a projectile still waiting to
    // leave does not.
    void own_cast_ended(std::uint32_t caster, double now);
    // The server landed `spell` from `source` on `target` (OP_Action 231), `distance` apart (0 when not known).
    // Returns the seconds the spell's impact particles should wait for the projectile to arrive (0 when it is
    // there, or there is none).
    double landed(std::uint32_t spell, const ProjectileStyle& style, std::uint32_t source, std::uint32_t target,
                  double now, float distance = 0.0f);
    // A bolt's flight over `distance`.
    static double bolt_flight(const ProjectileStyle& style, float distance);
    // A server missile was launched from `source` at `target`: a projectile of the source's last cast follows it,
    // when that spell has one. True when it does (the missile's own model is not drawn then).
    bool follow_missile(std::uint32_t serial, std::uint32_t source, std::uint32_t target, double now);

    void update(double now, const ProjectileWorld& world);
    void clear();

    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    const std::vector<ProjectileSpark>& sparks() const { return sparks_; }
    // The missiles projectiles follow (their models are hidden).
    bool follows(std::uint32_t serial) const;

    // A bolt's or comet's place `s` (0 the start .. 1 the target) along its path.
    static glm::vec3 path_point(const Projectile& p, float s);
    // How far through its flight it is at `now` (0..1).
    static float flight(const Projectile& p, double now);

private:
    Projectile& add(std::uint32_t spell, const ProjectileStyle& style, std::uint32_t caster, std::uint32_t target,
                    double now);
    void launch(Projectile& p, double now, double flight_seconds);
    void burst(Projectile& p, double now);
    void spark(const Projectile& p, double now, int index, bool burst);

    std::vector<Projectile> projectiles_;
    std::vector<ProjectileSpark> sparks_;
    struct LastCast {
        std::uint32_t spell = 0;
        ProjectileStyle style;
        double at = 0.0;
    };
    std::unordered_map<std::uint32_t, LastCast> last_cast_;
    std::uint32_t counter_ = 0;
    std::optional<double> last_update_;
};

} // namespace nmsc::game
