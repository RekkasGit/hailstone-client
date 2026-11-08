#pragma once

// The target ring at the current target's feet (game/target_indicator.h; docs/graphics.md "The target ring"), drawn
// as a decal over what the scene's depth shows there (shaders/target_ring.frag): it lies on the ground under the
// target, follows slopes and steps, and whatever stands in front of it (the target's own legs, other characters,
// walls) hides it. Where no ground lies near the feet (a target swimming or in the air), it is a level ring at the
// feet, hidden the same way. Needs VkContext::begin_translucent's depth copy, so it is drawn after that.

#include <array>
#include <filesystem>

#include <glm/glm.hpp>

#include "render/gpu.h"
#include "render/vk_context.h"

namespace hailstone::render {

struct TargetRing {
    glm::vec3 feet{0.0f};
    float radius = 2.0f;
    glm::vec4 color{1.0f};   // the section's colour (display sRGB) and alpha, 0..1
    float width = 0.15f;     // the band's half width, world units
    float spin = 0.0f;       // the dashes' turn, radians
    float facing = 0.0f;     // the target's yaw (game::heading_to_yaw)
    float pulse = 0.0f;      // 0..1
    float flash = 0.0f;      // the sweep on a new target, 1 .. 0
    float xray = 0.0f;       // how strongly the hidden part shows through, 0 none
    float glow = 0.7f;       // how far past the colour the core goes (game::target_ring_core_boost)
};

// The frame's terms the decal needs.
struct TargetRingView {
    glm::mat4 view_proj{1.0f};
    glm::vec3 camera{0.0f};
    glm::vec3 forward{0.0f, 1.0f, 0.0f};
    float near_plane = 1.0f, far_plane = 1000.0f;
    glm::vec4 haze{0.0f}; // SceneLighting::haze (z 0: none)
    double seconds = 0.0;
};

class TargetRingRenderer {
public:
    TargetRingRenderer(VkContext& vk, const std::filesystem::path& shader_dir);
    ~TargetRingRenderer();
    TargetRingRenderer(const TargetRingRenderer&) = delete;
    TargetRingRenderer& operator=(const TargetRingRenderer&) = delete;

    // After VkContext::begin_translucent.
    void draw(const FrameInfo& frame, const TargetRingView& view, const TargetRing& ring);

private:
    void create_pipeline(const std::filesystem::path& shader_dir);

    VkContext& vk_;
    std::array<Buffer, VkContext::kFramesInFlight> instances_{};
    std::array<void*, VkContext::kFramesInFlight> mapped_{};
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace hailstone::render
