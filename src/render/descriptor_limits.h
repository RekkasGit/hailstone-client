#pragma once

// How many textures a renderer's bindless array (sampler2D textures[], one combined image sampler each) may hold in
// the fragment stage, from the device's descriptor limits (render/vk_context.h DeviceCaps::max_sampled_images).
//
// Each element counts as a sampler, a sampled image and a resource. The per-stage limits also cover what the same
// stage sees in the pipeline's other sets: the shadow set's samplers (VkContext::shadow_set_layout) and a few buffers
// and the colour attachment. Intel's Windows driver is the tight case: 64 samplers and 200 resources per stage,
// against 1048576 for sets made update-after-bind, which is why those sets are used where the device has them.

#include <algorithm>
#include <cstdint>

namespace nmsc::render {

// One kind of limit, either the plain ones (VkPhysicalDeviceLimits) or the update-after-bind ones
// (VkPhysicalDeviceVulkan12Properties).
struct DescriptorLimits {
    std::uint32_t per_stage_samplers = 0;
    std::uint32_t per_stage_sampled_images = 0;
    std::uint32_t per_stage_resources = 0;
    std::uint32_t set_samplers = 0;
    std::uint32_t set_sampled_images = 0;
};

// The shadow set's five combined image samplers (render/vk_context.cpp create_shadow_resources).
inline constexpr std::uint32_t kSharedFragmentSamplers = 5;
// Those, the buffers beside an array (the zone's uniform buffer and three light-grid buffers, the actors' lighting)
// and the colour attachment, with room to spare.
inline constexpr std::uint32_t kSharedFragmentResources = 16;

inline std::uint32_t texture_array_limit(const DescriptorLimits& l) {
    auto less = [](std::uint32_t v, std::uint32_t shared) { return v > shared ? v - shared : 0u; };
    std::uint32_t n = std::min({less(l.per_stage_samplers, kSharedFragmentSamplers),
                                less(l.per_stage_sampled_images, kSharedFragmentSamplers),
                                less(l.per_stage_resources, kSharedFragmentResources), l.set_samplers,
                                l.set_sampled_images});
    return std::max(1u, n);
}

} // namespace nmsc::render
