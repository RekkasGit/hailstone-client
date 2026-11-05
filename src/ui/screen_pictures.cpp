#include "ui/screen_pictures.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <system_error>

#include <imgui.h>

namespace nmsc::ui {

std::vector<std::filesystem::path> list_pictures(const std::filesystem::path& dir) {
    static constexpr std::array<std::string_view, 6> kExtensions{".png", ".jpg", ".jpeg", ".bmp", ".tga", ".dds"};
    std::vector<std::filesystem::path> out;
    if (dir.empty()) return out;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator();
         it.increment(ec)) {
        std::error_code type_ec;
        if (!it->is_regular_file(type_ec)) continue;
        std::string ext = it->path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (std::find(kExtensions.begin(), kExtensions.end(), ext) != kExtensions.end()) out.push_back(it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::filesystem::path> screen_pictures(const std::filesystem::path& client_dir,
                                                   const std::filesystem::path& server_dir,
                                                   const std::filesystem::path& shipped_dir) {
    for (const auto& dir : {server_dir, client_dir, shipped_dir})
        if (auto list = list_pictures(dir); !list.empty()) return list;
    return {};
}

void draw_screen_picture(ImDrawList* list, const ScreenPicture& picture) {
    if (!list || !picture.id || picture.width <= 0 || picture.height <= 0) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float scale = std::max(vp->Size.x / static_cast<float>(picture.width),
                                 vp->Size.y / static_cast<float>(picture.height));
    const ImVec2 size(static_cast<float>(picture.width) * scale, static_cast<float>(picture.height) * scale);
    // Centred across; held to the top (a title there stays whole on a wide screen), the overhang cut at the bottom.
    const ImVec2 p0(vp->Pos.x + (vp->Size.x - size.x) * 0.5f, vp->Pos.y);
    list->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(0, 0, 0, 255));
    list->AddImage(reinterpret_cast<ImTextureID>(picture.id), p0, ImVec2(p0.x + size.x, p0.y + size.y));
}

} // namespace nmsc::ui
