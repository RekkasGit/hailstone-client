#pragma once

// Spell particle emitters: the graphics DLL's CParticleSystem as far as
// rof2decomp notes/eqgraphicsdx9.md (Particles) and OpenEQ
// docs/SPELL_EFFECT_FORMAT.md describe it.
//
// eqgame's SpellEffect_SpawnStageEmitters @0x51aa20 walks the four emitter
// slots of an effect stage and calls CreateSpellEmitter (effect manager vtable
// +0x18, 0x10070580 -> CEmitterDefinitionCollection_CreateEmitter @0x10070440
// -> CEmitterDef::CreateEmitter @0x1006c920) for each:
//   - a slot is skipped when its index is < 1 or the caster's level is below
//     the slot's minimum (level 1 for a missing caster);
//   - the location picks the given point (0), the caster (1), the missile (2)
//     or the target (3); the attach id one of the actor's ten spell points
//     (assets/model_points.h; ActorClient +0x148, FUN_0058cd90): 1 point 0,
//     2..7 points 1..6 or else point 0, 8 the actor without a point, 9 / 10
//     the eyes (points 7 / 8; the slot is skipped without them), 11 the mouth
//     (point 9), 12 the actor without a point at the chest's x and y, scaled
//     by the actor height;
//   - density = eqclient.ini SpellParticleDensity and opacity =
//     SpellParticleOpacity (clamped to [0, 1]; both 1.0 with the flag);
//   - density <= 0 creates nothing; the duration is -EDD+0x78 when that is
//     negative, the requested ms x 0.001 when that is positive, else EDD+0x78;
//   - the stage's length is the longest emitter duration, in ms.
// Not traced (docs/unknowns.md): the particle capacity's x87 detail, EDD +0x13c/+0x140/+0x178 and the globals at
// +0x148..+0x168, and the exact motion integration; the simulation follows
// OpenEQ's field descriptions.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <vector>

#include <glm/glm.hpp>

#include "assets/spell_effects.h"

namespace hailstone::game {

// eqclient.ini [Defaults] SpellParticleDensity / SpellParticleOpacity.
struct SpellParticleSettings {
    float density = 1.0f;
    float opacity = 1.0f;
};
// From the ini values (missing -> 1.0), clamped to [0, 1].
SpellParticleSettings spell_particle_settings(std::optional<float> density, std::optional<float> opacity);

// Hailstone (not in the original): whose a spell effect stage is, for the Options window's per-source spell
// particle densities, which scale SpellParticleDensity. The original has one density and an "On For" filter
// (SpellParticleCastFilter, FUN_00514bd0: self, group, raid, all players, and NPCs as a bit); this client offers the
// three densities instead and does not read the filter.
enum class SpellFxSource : std::uint8_t { Self = 0, Players = 1, Npcs = 2 };
inline constexpr std::size_t kSpellFxSourceCount = 3;
// `caster` cast the spell and `struck` is the spawn it lands on (the caster again for the cast stage); `me` the
// player. A spell the player cast, or one that lands on the player, is Self (as FUN_00514bd0 always shows those). Any
// other is the caster's kind: NPCs (pets and mercenaries included), else Players. Without a known caster, the struck
// spawn's kind; without either, Players.
SpellFxSource spell_fx_source(std::uint32_t caster, std::uint32_t struck, std::uint32_t me,
                              std::optional<bool> caster_is_npc, std::optional<bool> struck_is_npc);

// CreateSpellEmitter's arguments (eqgame's order; see the table in the notes).
struct SpellEmitterRequest {
    std::int32_t emitter = 0;     // 1: EDD record index
    std::uint32_t key = 0;        // 2: spawn id << 16 | spell id
    std::int32_t duration_ms = 0; // 3: requested duration
    float density = 1.0f;         // 4
    float opacity = 1.0f;         // 5
    glm::vec3 position{0.0f};     // 6
    std::uint32_t actor = 0;      // 7: attach actor (spawn id), 0 = none
    std::int32_t bone_point = -1; // 9: spell point index (0..9), -1 = none
    float scale = 1.0f;           // 11: 1.0, or the actor height for attach 12
    // An actor emitter that stays with its actor until stopped (app/actor_emitters.h).
    bool persistent = false;
};

// An actor a stage can place emitters on.
struct StageActor {
    std::uint32_t id = 0;
    glm::vec3 position{0.0f};
    std::int32_t level = 1;
    float height = 6.0f;
    // Which of the ten spell points the actor's model has (bit n: point n), and
    // the chest's (point 1's) world position when it has one.
    std::uint16_t points = 0;
    glm::vec3 chest{0.0f};
    bool has_point(int n) const { return n >= 0 && n < 16 && (points >> n) & 1u; }
};

// SpellEffect_SpawnStageEmitters' slot loop for stage 0 (cast), 1
// (projectile) or 2 (impact).
std::vector<SpellEmitterRequest> spawn_stage_emitters(const assets::SpellEffect& effect, int stage,
                                                      std::uint32_t spell_id, const StageActor* caster,
                                                      const StageActor* target, const glm::vec3& point,
                                                      std::int32_t duration_ms, const SpellParticleSettings& settings,
                                                      bool full = false);

// CEmitterDef::CreateEmitter's duration in seconds.
float emitter_duration_seconds(float edd_duration, std::int32_t requested_ms);
// The particle capacity: the x87 detail is not traced; here initial + per tick
// x ticks per second x min(lifetime, duration) + per tick, the EDD's +0x144
// when set, capped at 100,000.
std::uint32_t emitter_capacity(const assets::EmitterDefinition& def, float duration);
// The basis a WLD particle node gives its emitter (columns transverse 1,
// transverse 2, axial in world), from the node's world rotation `owner`
// (columns: its local axes in world; normalised here) and the definition's
// +0xc4 / +0xc8 turns (512ths). The update (0x10072ab0 in OpenEQ's build,
// docs/WLD_PARTICLE_RUNTIME.md) takes the emitter axes from the node's world
// matrix rows 2, 0 and -1 as (axial, transverse 1, transverse 2); +0xc8 then
// turns about transverse 1 (-128 brings the axis onto the node's +Y, the
// witnessed lamp case) and +0xc4 about transverse 2 (128 onto +X, 256 onto -Z;
// the sign of this one is inferred from the converter's direction compares,
// not witnessed).
glm::mat3 emitter_axes(const glm::mat3& owner, float angle1, float angle2);

// The emission-rate factor for an emitter `distance` from the viewer: the
// definition's +0xa0 over the distance, at most 1, but not below
// min(1, 16 / capacity) (the emitter's batch floor).
float emission_distance_factor(float reduce_distance, std::uint32_t capacity, float distance);

// The flipbook grid (columns, rows) for a frame count: 1 -> 1x1, 2..4 -> 2x2,
// 5..8 -> 4x2, 9..16 -> 4x4, other -> 1x1.
glm::uvec2 flipbook_grid(std::uint32_t frames);

struct Particle {
    glm::vec3 position{0.0f};  // world
    float azimuth = 0.0f;      // radians: the radial direction while the particle is still on the axis
    glm::vec3 local{0.0f};     // basis-local position (axial, transverse 1, transverse 2)
    glm::vec3 velocity{0.0f};  // basis-local: axial, transverse 1, transverse 2
    float radial_speed = 0.0f, orbit_speed = 0.0f; // units / s, 512ths / s
    glm::vec3 world_velocity{0.0f}; // gravity and wind
    glm::vec3 drift{0.0f};          // world displacement from them
    glm::vec3 anchor{0.0f};         // emitter origin at spawn (particles that do not follow)
    float age = 0.0f, lifetime = 1.0f;
    float width = 1.0f, height = 1.0f;
    float rotation = 0.0f, spin = 0.0f; // 512ths, 512ths / s
    glm::vec4 color{1.0f}; // rgb, alpha; set by update
    float size_factor = 1.0f; // grow / shrink ramps; set by update
    std::uint32_t frame = 0;
};

struct Emitter {
    std::size_t definition = 0;
    std::uint32_t key = 0;
    std::uint32_t actor = 0;
    std::int32_t point = -1; // the actor's spell point it hangs from, or -1
    glm::vec3 origin{0.0f};
    // World directions of the basis-local transverse 1, transverse 2 and axial
    // coordinates (columns). Spell emitters keep (+X, -Y, +Z); a zone fixture
    // takes its owner node's (emitter_axes).
    glm::mat3 axes{glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
    // A zone fixture: emits for as long as it exists (see add_fixture).
    bool persistent = false;
    // Its particles are set from outside (set_still) and drawn as they are: no emission, no ageing.
    bool still = false;
    float scale = 1.0f;
    float density = 1.0f, opacity = 1.0f;
    float duration = 0.0f, age = 0.0f;
    float tick_accumulator = 0.0f;
    bool started = false;
    std::uint32_t capacity = 0;
    std::vector<Particle> particles;
};

// Where an attached emitter is this frame: its actor's spell point (the
// point's world position and its axes, columns) or, without a point, the
// actor's origin (no axes). The DLL's update (FUN_10072160) takes a pointed
// emitter's position from the point's world matrix and an unpointed one's
// from the actor (vtable +0x7c) every frame.
struct EmitterAttachment {
    glm::vec3 position{0.0f};
    std::optional<glm::mat3> axes;
};
using AttachmentFn = std::function<std::optional<EmitterAttachment>(std::uint32_t actor, std::int32_t point)>;

class ParticleSystem {
public:
    ParticleSystem() = default;
    explicit ParticleSystem(std::vector<assets::EmitterDefinition> definitions, std::uint32_t seed = 1);

    bool empty() const { return definitions_.empty(); }
    const std::vector<assets::EmitterDefinition>& definitions() const { return definitions_; }
    const std::vector<Emitter>& emitters() const { return emitters_; }

    // CreateSpellEmitter: the emitter's duration in ms, or 0 when none was
    // created (index out of range, density <= 0).
    std::int32_t create(const SpellEmitterRequest& request);
    // A zone fixture's emitter (a WLD particle node of a placed object): at
    // the node's world matrix, along emitter_axes, scaled by the node's scale
    // when the definition's +0x6c is set. It is persistent: the DLL creates
    // it with 9999 s and the actor keeps it while the object stands; here it
    // never runs out. Returns false for a definition index out of range.
    bool add_fixture(std::size_t definition, const glm::mat4& node_world, std::uint32_t key = 0);
    // The viewer's position for the distance-scaled emission rate
    // (emission_distance_factor); nullopt (the default) emits at full rate.
    void set_viewer(std::optional<glm::vec3> viewer) { viewer_ = viewer; }
    // Stops every emitter with this key (effect manager vtable +0x38).
    void stop(std::uint32_t key);
    // The density of the persistent emitters (zone fixtures, actors' own): what they emit from now on scales with
    // it (the effect manager's density setters, +0xa4 environment / +0xac actor).
    void set_persistent_density(float density);
    // Moves the emitters with this key (the effect's position, vtable +0x14).
    void move(std::uint32_t key, const glm::vec3& origin);
    bool has(std::uint32_t key) const;
    // A definition of the caller's (a texture to draw still particles with); returns its index.
    std::size_t add_definition(assets::EmitterDefinition d);
    // The particles of the still emitter `key` (of `definition`), drawn as given until set again or stopped.
    void set_still(std::uint32_t key, std::size_t definition, std::vector<Particle> particles);
    void clear() { emitters_.clear(); }
    // Advances by dt seconds; `attachment` places the attached emitters. An
    // emitter on a point whose definition takes the bone's orientation (+0x60)
    // turns its axes with the point (emitter_axes of the point's axes and the
    // definition's +0xc4 / +0xc8 turns).
    void update(float dt, const AttachmentFn& attachment);

private:
    void spawn(Emitter& e, std::uint32_t count);
    float random(float a, float b);

    std::vector<assets::EmitterDefinition> definitions_;
    std::vector<Emitter> emitters_;
    std::mt19937 rng_{1};
    std::optional<glm::vec3> viewer_;
};

} // namespace hailstone::game
