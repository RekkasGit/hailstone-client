#pragma once

// The radial flora of EQG heightmap zones (assets/flora.h; /showgrass, the Options window's Radial Flora): the plants
// of the tiles near the camera, each a quad standing on its foot, or two crossed ones (*CROSSFLORA), turned as it was
// placed, its picture a quarter of its texture with *QUADTEXTURE, swaying (*SWAY: the top moves *SWAYDISPLACEMENT of
// its height to and fro over *SWAYPERIOD seconds). The look is this client's (docs/graphics.md "Radial flora"): cut
// out at half alpha, lit per plant as the particles' smoke is (render::particle_light) and hazed with distance; the
// plants shrink into the ground over the last fifth of the reach, as the original draws flora only round the camera.

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <glm/glm.hpp>

#include "render/gpu.h"
#include "render/vk_context.h"

namespace nmsc::assets { struct Zone; }

namespace nmsc::render {

struct SceneLighting;

// How far round the camera the flora is drawn (the original's radius is not traced; docs/unknowns.md).
inline constexpr float kFloraReach = 400.0f;
// Hailstone's Flora Distance (display.flora_distance) reaches up to this far. Past kFloraReach the plants thin out
// with distance (flora_keep), so the far ring costs about what the near circle does.
inline constexpr float kMaxFloraReach = 1200.0f;

// The share of the plants kept at distance `d`: all within kFloraReach, (kFloraReach / d)^2 past it.
float flora_keep(float d);

class FloraRenderer {
public:
    static constexpr std::uint32_t kMaxPlants = 65536;
    static constexpr std::uint32_t kMaxTextures = 64; // at most; fewer where the device allows fewer

    // `zone` must outlive this (its archives give the textures and it holds the plants).
    FloraRenderer(VkContext& vk, const assets::Zone& zone, const std::filesystem::path& shader_dir);
    ~FloraRenderer();
    FloraRenderer(const FloraRenderer&) = delete;
    FloraRenderer& operator=(const FloraRenderer&) = delete;

    // How far the plants are drawn: kFloraReach .. kMaxFloraReach (display.flora_distance).
    void set_reach(float reach);
    float reach() const { return reach_; }

    // The plants within reach() of `camera`, nearest tiles first, in the opaque part of the scene (before
    // VkContext::begin_translucent). `lighting`: the frame's (modern: lit and hazed per plant; classic: the scene's
    // ambient and sun). Returns how many were drawn.
    std::uint32_t draw(const FrameInfo& frame, const glm::mat4& view_proj, const glm::vec3& camera, double seconds,
                       const SceneLighting& lighting, bool modern);

private:
    void create_pipeline(const std::filesystem::path& shader_dir);

    VkContext& vk_;
    float reach_ = kFloraReach;
    std::vector<std::pair<float, std::uint32_t>> near_tiles_; // per draw: tiles in reach by distance
    std::uint32_t max_textures_ = 0; // kMaxTextures, within DeviceCaps::max_sampled_images
    const assets::Zone& zone_;
    std::vector<Texture> textures_;             // slot 0: white
    std::vector<std::uint32_t> definition_slot_; // per flora definition
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    std::array<Buffer, VkContext::kFramesInFlight> instances_{};
    std::array<void*, VkContext::kFramesInFlight> mapped_{};
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace nmsc::render
