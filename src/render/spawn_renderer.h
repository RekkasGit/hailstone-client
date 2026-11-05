#pragma once

// Placeholder spawn rendering until character models (M4): one coloured,
// heading-aligned box per spawn, drawn instanced.

#include <array>
#include <cstdint>
#include <filesystem>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "render/gpu.h"
#include "render/vk_context.h"

namespace nmsc::game {
struct ViewDistance;
struct WorldState;
}

namespace nmsc::render {

class SpawnRenderer {
public:
    SpawnRenderer(VkContext& vk, const std::filesystem::path& shader_dir);
    ~SpawnRenderer();
    SpawnRenderer(const SpawnRenderer&) = delete;
    SpawnRenderer& operator=(const SpawnRenderer&) = delete;

    // hide_id: a spawn not to draw (the local player in first person), or 0.
    // skip: spawns already drawn another way (as character models), or null.
    void draw(const FrameInfo& frame, const glm::mat4& view_proj, const glm::vec3& camera_pos,
              const game::ViewDistance& view, const game::WorldState& world, std::uint32_t hide_id,
              const std::unordered_set<std::uint32_t>* skip = nullptr);

private:
    struct Instance {
        glm::mat4 model;
        glm::vec4 color;
    };
    static constexpr std::uint32_t kMaxInstances = 4096;

    VkContext& vk_;
    Buffer box_;
    std::array<Buffer, VkContext::kFramesInFlight> instances_{};
    std::array<void*, VkContext::kFramesInFlight> mapped_{};
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace nmsc::render
