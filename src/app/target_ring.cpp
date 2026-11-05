// ClientApp: the 3D target indicator, the ring at the current target's feet (game/target_indicator.h).

#include "app/client_app_internal.h"

#include "core/ini_file.h"

namespace nmsc::app::client_internal {

void ClientApp::draw_target_ring(const glm::mat4& view_proj, VkExtent2D extent) {
    // TargetIndicator_Update @0x5c5650 → TargetIndicator_UpdateTargetRing @0x5c5250: hidden without a target or
    // with the indicator off (Show3dTargetIndicator, TargetIndicator +0); otherwise the section of the target's
    // con, hidden for con 0.
    if (!conn_ || !conn_->client_option(game::ClientOption::ShowTargetRing)) return;
    const auto& world = conn_->world();
    auto it = world.target_id ? world.spawns.find(world.target_id) : world.spawns.end();
    if (it == world.spawns.end()) return;
    const game::Spawn& s = it->second;
    auto section = game::target_ring_section(game::con_level(s, conn_->player_status().level, conn_->pvp_rules()));
    if (!section) return;
    if (!target_indicator_) {
        // TargetIndicator_LoadIni on the first update: the skin's TargetIndicator.ini; this client's skin is its
        // Lua UI, so the default UI's.
        IniFile ini;
        if (auto bytes = game_.read("uifiles/default/TargetIndicator.ini"))
            ini.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
        target_indicator_ = game::TargetIndicatorSettings::parse(ini);
    }
    const game::TargetRingStyle& style = target_indicator_->styles[static_cast<std::size_t>(*section)];

    bool has_actor = actor_renderer_ && actor_renderer_->model_for(s);
    float radius = game::target_ring_radius(s.heights.avatar_height, s.heights.model_scale, has_actor);
    glm::vec3 base = s.display_valid ? s.display_position : s.position;
    glm::vec3 feet = base - glm::vec3(0, 0, s.heights.avatar_height);

    // Drawn this client's way: a closed line on the floor, in the section's colour and alpha.
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    // The 3D view's pixels to its place in the window (/viewport).
    float sx = view_area_.w / static_cast<float>(extent.width), sy = view_area_.h / static_cast<float>(extent.height);
    constexpr int kPoints = 48;
    ImVec2 pts[kPoints];
    for (int i = 0; i < kPoints; ++i) {
        float a = static_cast<float>(i) * glm::two_pi<float>() / static_cast<float>(kPoints);
        glm::vec4 c = view_proj * glm::vec4(feet + glm::vec3(std::cos(a) * radius, std::sin(a) * radius, 0.1f), 1.0f);
        if (c.w <= 0.1f) return; // partly behind the camera
        pts[i] = ImVec2(view_area_.x + (c.x / c.w * 0.5f + 0.5f) * static_cast<float>(extent.width) * sx,
                        view_area_.y + (0.5f - c.y / c.w * 0.5f) * static_cast<float>(extent.height) * sy);
    }
    ImU32 colour = IM_COL32(style.red, style.green, style.blue, style.alpha);
    dl->PushClipRect(ImVec2(view_area_.x, view_area_.y), ImVec2(view_area_.x + view_area_.w, view_area_.y + view_area_.h));
    dl->AddPolyline(pts, kPoints, colour, ImDrawFlags_Closed, 2.5f);
    dl->PopClipRect();
}

} // namespace nmsc::app::client_internal
