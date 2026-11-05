#include "app/spell_particles.h"

#include <algorithm>
#include <cstdlib>

#include "assets/character.h"
#include "core/game_dir.h"
#include "core/ini_file.h"
#include "core/log.h"
#include "game/spells.h"
#include "net/game_connection.h"
#include "render/actor_renderer.h"

namespace nmsc::app {

SpellParticles::SpellParticles(render::VkContext& vk, const GameDir& game, const std::filesystem::path& shader_dir) {
    try {
        if (auto data = game.read("spellsnew.edd")) particles_ = game::ParticleSystem(assets::parse_edd(*data));
        if (auto data = game.read("spellsnew.eff")) effects_ = assets::parse_spell_effects(*data);
    } catch (const std::exception& e) {
        log::warn("spell particles: {}", e.what());
    }
    IniFile ini;
    if (auto path = game.find("eqclient.ini")) ini.load(*path);
    auto value = [&](std::string_view key) -> std::optional<float> {
        auto v = ini.get("Defaults", key);
        if (!v || v->empty()) return std::nullopt;
        return std::strtof(v->c_str(), nullptr);
    };
    settings_ = game::spell_particle_settings(value("SpellParticleDensity"), value("SpellParticleOpacity"));
    eqclient_density_ = settings_.density;
    if (auto path = game.find("Resources/SPOffsets.ini")) sp_offsets_.load(*path);
    spell_points_ = assets::SpellPoints([this](std::string_view section, std::string_view key) {
        std::optional<float> v;
        if (auto text = sp_offsets_.get(section, key)) v = std::strtof(text->c_str(), nullptr);
        return v;
    });
    if (!particles_.empty()) {
        renderer_ = std::make_unique<render::ParticleRenderer>(vk, game, shader_dir);
        log::info("spell particles: {} emitter definitions, {} effects, density {:.2f}, opacity {:.2f}",
                  particles_.definitions().size(), effects_.size(), settings_.density, settings_.opacity);
    }
}

std::optional<glm::mat4> SpellParticles::point_world(const render::ActorRenderer& actors, std::uint32_t id, int point) {
    const assets::CharacterModel* model = actors.drawn_model(id);
    if (!model || point < 0 || point >= 10) return std::nullopt;
    const assets::SpellPoints::Model& sp = spell_points_.of(*model);
    if (!((sp.mask >> point) & 1u)) return std::nullopt;
    glm::mat4 bone(1.0f);
    if (!actors.drawn_bone(id, sp.bone[static_cast<std::size_t>(point)], bone)) return std::nullopt;
    return bone * sp.local[static_cast<std::size_t>(point)];
}

void SpellParticles::update(net::GameConnection& conn, std::span<const net::SpellEffectCue> cues,
                            const render::ActorRenderer* actors, float dt) {
    clock_ += dt;
    for (const auto& cue : cues) {
        Held h{clock_ + cue.delay, cue.stage, cue.caster, cue.target, cue.spell, cue.duration_ms};
        if (cue.delay > 0.0) held_.push_back(h);
        else play(conn, h, actors);
    }
    for (std::size_t i = 0; i < held_.size();) {
        if (held_[i].at > clock_) {
            ++i;
            continue;
        }
        Held h = held_[i];
        held_.erase(held_.begin() + static_cast<std::ptrdiff_t>(i));
        play(conn, h, actors);
    }
    update_path(conn);
    const auto& world = conn.world();
    auto position = [&](std::uint32_t id) -> std::optional<glm::vec3> {
        auto it = world.spawns.find(id);
        if (it == world.spawns.end()) return std::nullopt;
        const auto& s = it->second;
        return s.display_valid ? s.display_position : s.position;
    };
    particles_.update(dt, [&](std::uint32_t id, std::int32_t pt) -> std::optional<game::EmitterAttachment> {
        if (pt >= 0 && actors)
            if (auto m = point_world(*actors, id, pt)) return game::EmitterAttachment{glm::vec3((*m)[3]), glm::mat3(*m)};
        if (auto p = position(id)) return game::EmitterAttachment{*p, std::nullopt};
        return std::nullopt;
    });
}

namespace {
// The keys of the path's emitters (above any spawn id << 16 | spell id a spell's emitters use).
constexpr std::uint32_t kPathLineKey = 0xfff00000u;
constexpr std::uint32_t kPathFinishKey = 0xfff00001u;
std::uint32_t mover_key(std::uint32_t serial, int i) { return 0xfff10000u + ((serial & 0x3fffu) << 2) + static_cast<std::uint32_t>(i); }
} // namespace

void SpellParticles::update_path(net::GameConnection& conn) {
    if (particles_.empty()) return;
    const game::PlayerPath& path = conn.player_path();
    const game::PathSettings& ps = path.settings();
    // The line: soft glowing quads along PlayerPath's spline points (and half way between them), their alpha as
    // PlayerPath fades the line near and far from the player. This client's look: the original draws a textured tube
    // (ThickLineSystem with Path.ini's Texture and ShapeSet); a soft blob (spellsnew's blobb.dds, a finish emitter's
    // texture) reads better as camera-facing quads than the tube's stripe texture.
    if (!path_definition_) {
        assets::EmitterDefinition d;
        d.name = "PlayerPath";
        d.texture = "blobb.dds";
        d.additive = 1;
        d.no_zwrite = 1;
        d.frames = 1;
        path_definition_ = particles_.add_definition(std::move(d));
    }
    std::vector<game::Particle> quads;
    if (path.active()) {
        const auto& line = path.line();
        auto quad = [&](const glm::vec3& at, float scale, float alpha) {
            if (alpha <= 0.0f) return;
            game::Particle q;
            q.position = at;
            q.width = q.height = 2.5f * scale;
            q.color = glm::vec4(0.75f, 0.9f, 1.0f, alpha);
            q.lifetime = 1.0f;
            quads.push_back(q);
        };
        for (std::size_t i = 0; i < line.size(); ++i) {
            quad(line[i].position, line[i].scale, line[i].alpha);
            if (i + 1 < line.size())
                quad((line[i].position + line[i + 1].position) * 0.5f, (line[i].scale + line[i + 1].scale) * 0.5f,
                     std::min(line[i].alpha, line[i + 1].alpha));
        }
    }
    if (path_definition_) {
        if (quads.empty()) particles_.stop(kPathLineKey);
        else particles_.set_still(kPathLineKey, *path_definition_, std::move(quads));
    }
    // The movers: their four EmitterN emitters made where a mover is first placed, moved along with it, and taken away
    // when it is off the path or gone (a reused mover has a new serial).
    std::vector<std::uint32_t> live;
    if (path.active())
        for (const auto& m : path.movers()) {
            auto at = path.mover_position(m);
            if (!at) continue;
            live.push_back(m.serial);
            for (int i = 0; i < 4; ++i) {
                const auto& es = ps.emitters[static_cast<std::size_t>(i)];
                if (es.definition < 1) continue;
                std::uint32_t key = mover_key(m.serial, i);
                if (particles_.has(key)) {
                    particles_.move(key, *at);
                    continue;
                }
                game::SpellEmitterRequest r;
                r.emitter = es.definition;
                r.key = key;
                r.duration_ms = es.life_span;
                r.density = es.density * settings_.density;
                r.opacity = es.opacity * settings_.opacity;
                r.position = *at;
                r.persistent = true;
                particles_.create(r);
            }
        }
    for (std::uint32_t serial : path_movers_)
        if (std::find(live.begin(), live.end(), serial) == live.end())
            for (int i = 0; i < 4; ++i) particles_.stop(mover_key(serial, i));
    path_movers_ = std::move(live);
    // PlayerPath_OnDestinationReached: the FinishEmitterN at the destination for their LifeSpan.
    if (auto at = conn.take_path_finish())
        for (const auto& es : ps.finish_emitters) {
            if (es.definition < 1) continue;
            game::SpellEmitterRequest r;
            r.emitter = es.definition;
            r.key = kPathFinishKey;
            r.duration_ms = es.life_span;
            r.density = es.density * settings_.density;
            r.opacity = es.opacity * settings_.opacity;
            r.position = *at;
            particles_.create(r);
        }
}

void SpellParticles::play(net::GameConnection& conn, const Held& cue, const render::ActorRenderer* actors) {
    if (particles_.empty()) return;
    const auto& world = conn.world();
    auto position = [&](std::uint32_t id) -> std::optional<glm::vec3> {
        auto it = world.spawns.find(id);
        if (it == world.spawns.end()) return std::nullopt;
        const auto& s = it->second;
        return s.display_valid ? s.display_position : s.position;
    };
    auto actor = [&](std::uint32_t id) -> std::optional<game::StageActor> {
        auto it = world.spawns.find(id);
        if (it == world.spawns.end()) return std::nullopt;
        const auto& s = it->second;
        game::StageActor a;
        a.id = id;
        a.position = *position(id);
        a.level = s.level > 0 ? s.level : 1;
        a.height = s.heights.height; // Height (+0x13c): the size, or racedata's default
        if (actors)
            if (const assets::CharacterModel* model = actors->drawn_model(id)) {
                a.points = spell_points_.of(*model).mask;
                if (auto chest = point_world(*actors, id, 1)) a.chest = glm::vec3((*chest)[3]);
            }
        return a;
    };
    const game::SpellData* sp = conn.spell_table().get(cue.spell);
    if (!sp || sp->spell_anim < 1 || static_cast<std::size_t>(sp->spell_anim) >= effects_.size()) return;
    auto caster = actor(cue.caster);
    auto target = actor(cue.target);
    glm::vec3 point = caster ? caster->position : target ? target->position : glm::vec3(0.0f);
    std::int32_t longest = 0;
    for (const auto& r : game::spawn_stage_emitters(effects_[static_cast<std::size_t>(sp->spell_anim)], cue.stage,
                                                     cue.spell, caster ? &*caster : nullptr,
                                                     target ? &*target : nullptr, point, cue.duration_ms, settings_))
        longest = std::max(longest, particles_.create(r));
    log::debug("spell {} stage {}: effect {} ({} ms)", cue.spell, cue.stage, sp->spell_anim, longest);
}

void SpellParticles::prepare() {
    if (renderer_) renderer_->prepare(particles_);
}

void SpellParticles::draw(const render::FrameInfo& frame, const glm::mat4& view_proj, const glm::mat4& view,
                          float glow, const render::SceneLighting* modern) {
    if (renderer_) renderer_->draw(frame, view_proj, view, particles_, glow, {}, modern);
}

void SpellParticles::clear() {
    particles_.clear();
    held_.clear();
    path_movers_.clear();
}

std::vector<game::LightSource> SpellParticles::lights(std::size_t max) const {
    return game::particle_lights(
        particles_, [this](const std::string& t) { return renderer_ ? renderer_->texture_color(t) : glm::vec3(1.0f); },
        max);
}

} // namespace nmsc::app
