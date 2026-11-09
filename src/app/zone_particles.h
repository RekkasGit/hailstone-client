#pragma once

// The zone's standing particle emitters: the flames and smoke of torches,
// lanterns and braziers (classic zones' 0x34 particle clouds on the placed
// objects' skeleton nodes; assets/wld_particles.h, Zone::particle_fixtures)
// and an EQG zone's environment emitters (assets/zone_fires.h). One
// persistent game::ParticleSystem emitter per fixture, emitting at the
// distance-scaled rate, drawn by render::ParticleRenderer with the textures
// from the zone's archives. Unlike the spell particles they ignore the ini's
// SpellParticleDensity / SpellParticleOpacity (the WLD list is created without
// them).
//
// The flame clouds (assets::flame_emitter_hue) are kept apart from the rest, so
// the modern lighting can leave them out for its own flames (fires(): each one's
// size, measured by running its cloud for one particle lifetime: measure_flame_cloud).

#include <filesystem>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "assets/zone_fires.h"
#include "game/particles.h"
#include "render/particle_renderer.h"

namespace hailstone { class GameDir; }
namespace hailstone::assets { struct Zone; }

namespace hailstone::app {

// The longest a flame cloud is burnt before it is measured, s.
inline constexpr float kMaxFlameWarmup = 8.0f;

// A flame cloud's flame (base relative to the node's origin): a fresh emitter of `definition` at the node's rotation
// in `sim` (cleared first), burnt for one particle lifetime (at least 1 s, at most kMaxFlameWarmup) and then measured
// over a second of its bright particles. The sprites are teardrops filling about the middle of their quads. A default
// ZoneFire when the definition is out of range or shows nothing bright.
assets::ZoneFire measure_flame_cloud(game::ParticleSystem& sim, std::uint32_t definition, const glm::mat4& node);

class ZoneParticles {
public:
    // `zone` must outlive this (its archives supply the textures).
    ZoneParticles(render::VkContext& vk, const GameDir& game, const std::filesystem::path& shader_dir,
                  const assets::Zone& zone);

    bool empty() const { return particles_.emitters().empty(); }
    std::size_t emitters() const { return particles_.emitters().size(); }
    // Advances the emitters by dt seconds for a viewer at `viewer`, then loads
    // any texture they need (call outside a frame).
    void update(float dt, const glm::vec3& viewer);
    // Without `flames`, the flame clouds are left out. `modern`: render::ParticleRenderer::draw's.
    void draw(const render::FrameInfo& frame, const glm::mat4& view_proj, const glm::mat4& view, float glow = 1.0f,
              bool flames = true, const render::SceneLighting* modern = nullptr);
    // EnvironmentParticleDensity: what the clouds emit from now on scales with it (0 nothing).
    void set_density(float density) { particles_.set_persistent_density(density); }
    // The flame clouds as flames: where each stands and how big it burns.
    const std::vector<assets::ZoneFire>& fires() const { return fires_; }

private:
    game::ParticleSystem particles_;
    std::vector<std::uint8_t> flame_; // per emitter: 1 a flame cloud
    std::vector<assets::ZoneFire> fires_;
    std::unique_ptr<render::ParticleRenderer> renderer_;
};

} // namespace hailstone::app
