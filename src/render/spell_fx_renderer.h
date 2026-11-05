#pragma once

// The modern lighting's spell bolts and comets (game/spell_projectiles.h, docs/graphics.md "Spell bolts and
// comets"), drawn as procedural light: orbs, trails, sparks, bursts, shockwaves and lightning, each shaded by its
// element (shaders/spell_fx.frag). Added to the scene past 1 so it blooms; it fades where it meets the scene
// (VkContext::begin_translucent's depth copy, so it is drawn after that) and with the haze, as the fire does.

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>

#include <glm/glm.hpp>

#include "render/gpu.h"
#include "render/vk_context.h"

namespace nmsc::render {

struct SceneLighting;

// One piece to draw, in world units.
struct SpellFxPiece {
    enum class Kind : std::uint8_t { Orb, Trail, Spark, Burst, Ring, Beam };
    Kind kind = Kind::Orb;
    std::uint8_t element = 0; // game::SpellElement
    glm::vec3 a{0.0f};        // the point (a segment's head)
    float radius_a = 1.0f;
    glm::vec3 b{0.0f};        // a segment's tail
    float radius_b = 1.0f;
    float age = 0.0f;         // a burst's, ring's or fizzle's progress, 0..1
    float seed = 0.0f;
    float intensity = 1.0f;
    float phase = 0.0f;       // an orb's: 0 flying, 1 waiting at its target, 2 fizzling
};

class SpellFxRenderer {
public:
    static constexpr std::uint32_t kMaxPieces = 8192;

    SpellFxRenderer(VkContext& vk, const std::filesystem::path& shader_dir);
    ~SpellFxRenderer();
    SpellFxRenderer(const SpellFxRenderer&) = delete;
    SpellFxRenderer& operator=(const SpellFxRenderer&) = delete;

    // After VkContext::begin_translucent; `lighting` gives the haze and the depth copy's terms, `glow` scales the
    // light (kParticleGlow's role).
    void draw(const FrameInfo& frame, const glm::mat4& view_proj, const glm::vec3& camera, double seconds,
              std::span<const SpellFxPiece> pieces, const SceneLighting& lighting, float glow = 1.0f);

private:
    void create_pipeline(const std::filesystem::path& shader_dir);

    VkContext& vk_;
    std::array<Buffer, VkContext::kFramesInFlight> instances_{};
    std::array<void*, VkContext::kFramesInFlight> mapped_{};
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace nmsc::render
