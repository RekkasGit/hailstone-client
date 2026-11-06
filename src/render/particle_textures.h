#pragma once

// What render::ParticleRenderer puts up for one particle texture, made on its loader thread rather than in the frame
// (a new spell effect's first cast read, decoded and averaged its textures on the main thread). Kept apart from Vulkan
// so it is tested.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "assets/texture.h"

namespace nmsc { class GameDir; }

namespace nmsc::render {

struct LoadedParticleTexture {
    std::optional<assets::Image> image; // as decoded; nullopt when no folder has it or it does not decode
    glm::vec3 color{1.0f};              // ParticleRenderer::texture_color
};

// The texture `name` (lower case): `zone_bytes` (the zone's archives' copy) when given, else the first of the game
// folder's SpellEffects/, EnvEmitterEffects/ and ActorEffects/ that has the file; the first source that has it is the
// one used, decoded or not. Its colour is the mean of rgb x alpha, scaled so the brightest channel is 1 (white for
// black or none).
LoadedParticleTexture load_particle_texture(const std::string& name, std::optional<std::vector<std::byte>> zone_bytes,
                                            const GameDir& game);

} // namespace nmsc::render
