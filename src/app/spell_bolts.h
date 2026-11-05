#pragma once

// The modern lighting's spell bolts and comets in the game (game/spell_projectiles.h): fed the spell effect cues
// before SpellParticles takes them (the impact stage of another's projectile waits for it to arrive: the cue's
// delay), the player's own casts and the server's spell missiles; drawn by render::SpellFxRenderer; lighting what
// they pass. Nothing of it in the classic lighting.

#include <cstdint>
#include <functional>
#include <optional>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "game/light_list.h"
#include "game/spell_projectiles.h"
#include "render/spell_fx_renderer.h"

namespace nmsc::net { class GameConnection; struct SpellEffectCue; }

namespace nmsc::app {

// The projectiles' pieces to draw at `now` (appended to `out`), and their lights, the `max` brightest: shared by the
// game and the viewer's --spell-bolt.
void projectile_pieces(const game::SpellProjectiles& projectiles, double now, std::vector<render::SpellFxPiece>& out);
std::vector<game::LightSource> projectile_lights(const game::SpellProjectiles& projectiles, double now, std::size_t max);

class SpellBolts {
public:
    SpellBolts(render::VkContext& vk, const std::filesystem::path& shader_dir);

    // The frame's cues, before the spell particles take them: sets the impact cues' delays.
    void take_cues(const net::GameConnection& conn, std::span<net::SpellEffectCue> cues, double now);
    // A server missile was added (MissileList serial): true when a spell projectile follows it instead of its model.
    bool missile(std::uint32_t serial, std::uint32_t source, std::uint32_t target, double now);
    // `missile_at`: a followed missile's place, while it flies.
    void update(const net::GameConnection& conn, double now,
                const std::function<std::optional<glm::vec3>(std::uint32_t serial)>& missile_at);
    void draw(const render::FrameInfo& frame, const glm::mat4& view_proj, const glm::vec3& camera, double seconds,
              const render::SceneLighting& lighting, float glow);
    void clear();

    bool follows(std::uint32_t serial) const { return projectiles_.follows(serial); }
    // Their light: one at each projectile's head, a flash at each burst; the `max` brightest.
    std::vector<game::LightSource> lights(std::size_t max) const;

private:
    game::SpellProjectiles projectiles_;
    std::vector<render::SpellFxPiece> pieces_;
    std::unique_ptr<render::SpellFxRenderer> renderer_;
    bool was_casting_ = false;
    double now_ = 0.0;
};

} // namespace nmsc::app
