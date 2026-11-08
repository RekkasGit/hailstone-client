#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// At every start: the UI settings of the builds that kept them in the data folder's ui-settings/ are moved to
// platform::ui_settings_dir() (ui/settings/), inside the same Hailstone folder. A file already there is kept and the
// old one left; the old folder goes once empty. Returns the number of files moved.

namespace hailstone {

int move_old_ui_settings();

// At every start: the client's own windows were "Title##nmsc..." up to 0.6 and are "Title##hailstone..." since, and
// ImGui keeps a window's place and size in hailstone-windows.ini (platform::imgui_ini()) under that name. The old
// names there are renamed so the windows keep their places, once: a file that has a new name already is left as it
// is (a newer client has written it). The text with the names renamed, or the same text:
std::string rename_old_window_ids(std::string_view ini);
// The same on the file; true when it was rewritten.
bool rename_old_window_ids_in(const std::filesystem::path& ini_file);

} // namespace hailstone
