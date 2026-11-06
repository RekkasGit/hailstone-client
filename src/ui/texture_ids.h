#pragma once
// The ImTextureIDs the client has handed out (descriptor sets made by ImGui_ImplVulkan_AddTexture), so that the Lua
// bindings (ui.Image, ui.ImageButton, ui.DrawImage) draw only those: a script holds other userdata too (an mq.TLO
// value, a handle that has since been freed), and the Vulkan backend would bind any pointer as a descriptor set.
// Main thread only, as ImGui's draw calls are.
#include <unordered_set>

namespace nmsc::ui {

inline std::unordered_set<const void*>& live_texture_ids() {
    static std::unordered_set<const void*> ids;
    return ids;
}
inline void texture_id_added(const void* id) {
    if (id) live_texture_ids().insert(id);
}
inline void texture_id_removed(const void* id) { live_texture_ids().erase(id); }
inline bool texture_id_live(const void* id) { return id && live_texture_ids().contains(id); }

} // namespace nmsc::ui
