#pragma once

// The actors' own particle emitters (assets/model_points.h ModelEmitter): an
// EQG model's `<model>.prt` and a held EQG item's, e.g. a glowing weapon or a
// creature's flames. 0x1006b130 / 0x1006b290 make one through
// CParticleSystem vtable +0x20 (0x10070640, ActorEmittersNew.edd) for each
// always-on entry (trigger 0, variant below 1) whose model point exists, when
// the actor is made; it stays with the actor. Here the dressed mesh carries the
// entries (assets::MeshEmitter) and each drawn actor keeps a persistent emitter
// per entry on its point, placed every frame from the pose the actor renderer
// drew. Density and opacity: eqclient.ini [Defaults] ActorParticleDensity /
// ActorParticleOpacity. Of the animation-started entries (trigger 1; their
// animation is an index into eqgame's animation table at 0xaae3d8), those of
// STND (31) and IDLE (32) run while the actor stands: the rune circles'
// (#TPTrigger*, T05) blue lights. The others (footfalls, swings, casts) start
// each time their animation starts (or loops round) on the actor, after the
// entry's delay (+0x54), and emit while the animation plays, at most the
// entry's duration: the animation's clip is the table row's old-style code
// (new_to_old_anim, old_anim_clip). How the original times them is not
// traced; this is the client's own reading.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "game/particles.h"
#include "render/particle_renderer.h"

namespace hailstone { class GameDir; }
namespace hailstone::assets { struct CharacterMesh; }

namespace hailstone::app {

class ActorEmitters {
public:
    ActorEmitters(render::VkContext& vk, const GameDir& game, const std::filesystem::path& shader_dir);

    // An actor drawn this frame and the mesh it was drawn with.
    struct Actor {
        std::uint32_t id = 0;
        const assets::CharacterMesh* mesh = nullptr;
        bool standing = false; // in its stand or idle animation (render::ActorRenderer::standing)
    };
    // A bone's world matrix on an actor as drawn (false: not drawn).
    using BoneWorld = std::function<bool(std::uint32_t id, int bone, glm::mat4& world)>;
    // Whether the actor drawn last was playing the clip `code`, and how far in (render::ActorRenderer::drawn_playing).
    using Playing = std::function<bool(std::uint32_t id, std::string_view code, float& time)>;

    // Starts the emitters of actors that came or changed their mesh, stops those of actors gone, and advances them
    // by dt seconds for a viewer at `viewer`; then loads the textures they need (call outside a frame).
    void update(float dt, std::span<const Actor> actors, const BoneWorld& bone_world, const glm::vec3& viewer,
                const Playing& playing = {});
    // `modern`: render::ParticleRenderer::draw's.
    void draw(const render::FrameInfo& frame, const glm::mat4& view_proj, const glm::mat4& view, float glow = 1.0f,
              const render::SceneLighting* modern = nullptr);
    void clear();
    std::size_t emitters() const { return particles_.emitters().size(); }
    // ActorParticleDensity (the Options window's combo; the effect manager's +0xac): the actors' emitters are made
    // again with it. eqclient_density() is the ini's, read at the start.
    void set_density(float density);
    float density() const { return settings_.density; }
    float eqclient_density() const { return eqclient_density_; }

private:
    struct Live {
        const assets::CharacterMesh* mesh = nullptr;
        std::vector<std::uint32_t> keys;
        std::vector<std::uint32_t> standing_keys; // the STND / IDLE entries', while it stands
        // The other animation-started entries, by entry: the time into their animation last frame (negative: not
        // playing), a start waiting for the entry's delay (seconds left, negative none) and the emitter running.
        struct Triggered {
            float last = -1.0f, pending = -1.0f, length = 0.0f;
            std::uint32_t key = 0;
        };
        std::unordered_map<std::size_t, Triggered> triggered;
        bool seen = false;
    };
    game::ParticleSystem particles_;
    game::SpellParticleSettings settings_;
    float eqclient_density_ = 1.0f;
    std::unique_ptr<render::ParticleRenderer> renderer_;
    std::unordered_map<std::uint32_t, Live> live_;
    std::uint32_t next_key_ = 1;
};

} // namespace hailstone::app
