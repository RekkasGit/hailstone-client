#include "ui/ui_packs.h"

#include <algorithm>
#include <system_error>

#include "core/config.h"

namespace hailstone::ui {

namespace fs = std::filesystem;

std::vector<UiPack> list_ui_packs(const fs::path& root) {
    std::vector<UiPack> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        if (!e.is_directory(ec)) continue;
        Config c;
        if (!c.load(e.path() / "ui.ini")) continue;
        UiPack p;
        p.id = e.path().filename().string();
        p.name = c.get_string("name", p.id);
        p.description = c.get_string("description");
        p.input = c.get_string("input", "mouse");
        p.game_art = c.get_string("game_art", "icons") == "all";
        p.dir = e.path();
        out.push_back(std::move(p));
    }
    std::sort(out.begin(), out.end(), [](const UiPack& a, const UiPack& b) { return a.name < b.name; });
    return out;
}

const UiPack* find_ui_pack(const std::vector<UiPack>& packs, std::string_view id) {
    for (std::string_view want : {id, std::string_view("modern")})
        for (const auto& p : packs)
            if (p.id == want) return &p;
    return packs.empty() ? nullptr : &packs.front();
}

} // namespace hailstone::ui
