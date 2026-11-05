#pragma once

// The modern lighting's fire (docs/graphics.md, "Fire"): every flame the zone burns (torches, sconces, lanterns,
// braziers, campfires, candles) drawn as a procedural flame in place of the original's animated texture or particle
// cloud. Not the original's look: the flame is noise rising through a teardrop, hot white-yellow at its core and deep
// red at its edges, bright enough to bloom, with embers drifting up from it. Each flame stands on a quad that turns
// about the vertical to face the camera; it fades where it meets the scene (VkContext::begin_translucent's depth copy,
// so it is drawn after that) and with the haze.

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "assets/zone_fires.h"
#include "render/gpu.h"
#include "render/vk_context.h"

namespace nmsc::render {

struct SceneLighting;

// One flame: where it stands and how big it is, in world units.
struct FireSource {
    glm::vec3 base{0.0f};   // the middle of its foot
    float height = 4.0f;    // from its foot to the tips of its tongues
    float width = 2.0f;     // across its foot
    float seed = 0.0f;      // its own turbulence (any number; fires near each other differ)
    float intensity = 1.0f; // its brightness, 1 a torch's
    glm::vec3 tint{1.0f};   // a magic flame's hue (blue, green...): its brightness in this colour
    float recolor = 0.0f;   // how far the tint takes it: 0 ordinary fire, 1 wholly the tint
};

// A zone's fire (assets/zone_fires.h) as flames, appended to `out`: its hue's tint, a seed from where it stands. A
// wide fire (a campfire, a brazier) burns as a middle flame with smaller tongues round it rather than one broad one.
void add_fire_sources(const assets::ZoneFire& fire, std::vector<FireSource>& out);

class FireRenderer {
public:
    static constexpr std::uint32_t kMaxFires = 4096;

    FireRenderer(VkContext& vk, const std::filesystem::path& shader_dir);
    ~FireRenderer();
    FireRenderer(const FireRenderer&) = delete;
    FireRenderer& operator=(const FireRenderer&) = delete;

    // The flames within `range` of `camera`, after VkContext::begin_translucent. `lighting` gives the haze and the
    // depth copy's terms (set_haze, set_screen); `glow` scales the light (kParticleGlow's role).
    void draw(const FrameInfo& frame, const glm::mat4& view_proj, const glm::vec3& camera, double seconds,
              std::span<const FireSource> fires, const SceneLighting& lighting, float range = 1500.0f,
              float glow = 1.0f);

private:
    void create_pipeline(const std::filesystem::path& shader_dir);

    VkContext& vk_;
    std::array<Buffer, VkContext::kFramesInFlight> instances_{};
    std::array<void*, VkContext::kFramesInFlight> mapped_{};
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace nmsc::render
