#pragma once

// What a GPU needs for Hailstone (render/vk_context.cpp pick_physical_device), kept apart from Vulkan so the words a
// player sees when no GPU qualifies are tested. The README's "System requirements" lists the same.

#include <cstdint>
#include <string>
#include <vector>

namespace nmsc::render {

// What one GPU reports, read from its Vulkan properties and features.
struct GpuSupport {
    std::string name;
    std::uint32_t api_major = 0;
    std::uint32_t api_minor = 0;
    bool dynamic_rendering = false;
    bool synchronization2 = false;
    bool runtime_descriptor_array = false;
    bool nonuniform_sampled_image_indexing = false;
    bool descriptor_binding_partially_bound = false;
    bool descriptor_binding_variable_count = false;
    bool presents = false; // a queue family that draws and presents to the window
};

// What `gpu` lacks, one item each, in the order checked; empty when it can run Hailstone. Below Vulkan 1.3 only the
// version is named: the features are not read from such a device.
std::vector<std::string> gpu_missing(const GpuSupport& gpu);

// The error for no usable GPU: every GPU with what it lacks, or that none was found.
std::string no_usable_gpu_text(const std::vector<GpuSupport>& gpus);

} // namespace nmsc::render
