#pragma once
// ImGui_ImplVulkan_AddTexture / RemoveTexture, keeping the list of live texture IDs (ui/texture_ids.h) that the Lua
// bindings check.
#include <imgui_impl_vulkan.h>

#include "ui/texture_ids.h"

namespace hailstone::ui {

inline VkDescriptorSet add_imgui_texture(VkSampler sampler, VkImageView view, VkImageLayout layout) {
    VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(sampler, view, layout);
    texture_id_added(reinterpret_cast<const void*>(set));
    return set;
}
inline void remove_imgui_texture(VkDescriptorSet set) {
    texture_id_removed(reinterpret_cast<const void*>(set));
    ImGui_ImplVulkan_RemoveTexture(set);
}

} // namespace hailstone::ui
