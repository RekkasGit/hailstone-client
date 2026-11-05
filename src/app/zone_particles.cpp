#include "app/zone_particles.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <tuple>

#include "assets/zone.h"
#include "core/game_dir.h"
#include "core/log.h"

namespace nmsc::app {

namespace {

// A flame cloud's flame: its particles over a second of steady burning, from a fresh emitter of the same
// definition and node rotation (the origin aside, all a fixture's look depends on). The sprites are teardrops
// filling about the middle of their quads.
assets::ZoneFire measure_flame(game::ParticleSystem& sim, std::uint32_t definition, const glm::mat4& node) {
    glm::mat4 at = node;
    at[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    sim.clear();
    sim.add_fixture(definition, at);
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (int step = 0; step < 40; ++step) {
        sim.update(0.05f, {});
        if (step < 20) continue;
        for (const auto& p : sim.emitters().front().particles) {
            if (p.color.a < 0.15f) continue;
            glm::vec3 r(glm::vec2(p.width * 0.3f * p.size_factor), p.height * 0.35f * p.size_factor);
            lo = glm::min(lo, p.position - r);
            hi = glm::max(hi, p.position + r);
        }
    }
    assets::ZoneFire f;
    if (!(hi.x >= lo.x)) return f;
    f.base = glm::vec3((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, lo.z);
    // The cloud's reach is wider and taller than the flame it shows, its edges thin.
    f.width = std::max(hi.x - lo.x, hi.y - lo.y) * 0.75f;
    f.height = (hi.z - lo.z) * 0.85f;
    return f;
}

} // namespace

ZoneParticles::ZoneParticles(render::VkContext& vk, const GameDir& game, const std::filesystem::path& shader_dir,
                             const assets::Zone& zone)
    : particles_(zone.particle_definitions) {
    std::size_t made = 0;
    std::vector<assets::FlameHue> hues;
    hues.reserve(zone.particle_definitions.size());
    for (const auto& d : zone.particle_definitions) hues.push_back(assets::flame_emitter_hue(d));
    game::ParticleSystem sim(zone.particle_definitions);
    // The measured flames by definition and node rotation (rounded): most fixtures share a few.
    std::map<std::tuple<std::uint32_t, std::array<int, 9>>, assets::ZoneFire> measured;
    for (const auto& f : zone.particle_fixtures) {
        const bool flame = f.definition < hues.size() && hues[f.definition] != assets::FlameHue::None;
        if (!particles_.add_fixture(f.definition, f.transform)) continue;
        ++made;
        flame_.push_back(flame);
        if (!flame) continue;
        std::array<int, 9> rot{};
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r) rot[c * 3 + r] = static_cast<int>(std::lround(f.transform[c][r] * 20.0f));
        auto key = std::make_tuple(f.definition, rot);
        auto it = measured.find(key);
        if (it == measured.end()) it = measured.emplace(key, measure_flame(sim, f.definition, f.transform)).first;
        assets::ZoneFire fire = it->second;
        if (!(fire.height > 0.05f)) continue;
        fire.base += glm::vec3(f.transform[3]);
        fire.hue = hues[f.definition];
        fires_.push_back(fire);
    }
    if (made == 0) return;
    renderer_ = std::make_unique<render::ParticleRenderer>(vk, game, shader_dir);
    renderer_->set_texture_reader([&zone](const std::string& name) { return zone.read_file(name); });
    renderer_->set_rows_bottom_up(true);
    log::info("zone particles: {} emitters ({} flames, {} kinds measured) from {} cloud definitions", made,
              std::count(flame_.begin(), flame_.end(), 1), measured.size(),
              zone.particle_definitions.empty() ? 0 : zone.particle_definitions.size() - 1);
}

void ZoneParticles::update(float dt, const glm::vec3& viewer) {
    if (!renderer_) return;
    particles_.set_viewer(viewer);
    particles_.update(dt, {});
    renderer_->prepare(particles_);
}

void ZoneParticles::draw(const render::FrameInfo& frame, const glm::mat4& view_proj, const glm::mat4& view,
                         float glow, bool flames, const render::SceneLighting* modern) {
    if (renderer_)
        renderer_->draw(frame, view_proj, view, particles_, glow, flames ? std::span<const std::uint8_t>{} : flame_,
                        modern);
}

} // namespace nmsc::app
