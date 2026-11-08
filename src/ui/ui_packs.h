#pragma once

// The user interfaces the client can run: every folder of <client>/ui with a ui.ini (name, description, input,
// game_art) is one, its Lua window scripts in that folder. hailstone.ini ui.name picks one by folder name.

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace hailstone::ui {

struct UiPack {
    std::string id;          // the folder's name (ui.name)
    std::string name;        // shown in the Options window
    std::string description;
    std::string input;       // mouse, gamepad, touch (comma-separated)
    bool game_art = false;   // game_art = all: every uifiles/default file; else only the item, spell and gem icons
    std::filesystem::path dir;
};

// The UIs under `root`, by name; none when it has no folder with a ui.ini.
std::vector<UiPack> list_ui_packs(const std::filesystem::path& root);
// The UI `id` of `packs`, else "modern", else the first; null when there are none.
const UiPack* find_ui_pack(const std::vector<UiPack>& packs, std::string_view id);

} // namespace hailstone::ui
