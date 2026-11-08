#pragma once

// The window's icon in the title bar and the taskbar: icons/hailstone.png beside the binary (platform::icons_dir()).
// The project's own art, not the game's. Without the file the window keeps the system's icon.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "assets/texture.h"

namespace hailstone::platform {
class Window;
}

namespace hailstone::app {

// Largest side taken: SDL hands the pixels to the system as they are, and the icon is 256 x 256.
inline constexpr std::uint32_t kMaxWindowIconSide = 1024;

// The icon as RGBA8, or nullopt with *error set when the file is missing, not a PNG or too large.
std::optional<assets::Image> read_window_icon(const std::filesystem::path& file, std::string* error = nullptr);

// Sets icons_dir()/hailstone.png on the window; logs and leaves the system's icon when that fails.
void apply_window_icon(platform::Window& window);

} // namespace hailstone::app
