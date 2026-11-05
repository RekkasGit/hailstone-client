#include "core/old_ui_settings.h"

#include <filesystem>
#include <system_error>

#include "core/log.h"
#include "platform/paths.h"

namespace nmsc {

int move_old_ui_settings() {
    const std::filesystem::path old = platform::data_dir() / "ui-settings", now = platform::ui_settings_dir();
    std::error_code ec;
    if (!std::filesystem::is_directory(old, ec)) return 0;
    int moved = 0;
    for (const auto& e : std::filesystem::directory_iterator(old, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::filesystem::path to = now / e.path().filename();
        if (std::filesystem::exists(to, ec)) {
            log::warn("layout: {} left where it is: {} is there already", e.path().string(), to.string());
            continue;
        }
        std::filesystem::create_directories(now, ec);
        std::error_code mec;
        std::filesystem::rename(e.path(), to, mec);
        if (mec) {
            log::warn("layout: could not move {} to {}: {}", e.path().string(), to.string(), mec.message());
            continue;
        }
        ++moved;
    }
    if (std::filesystem::is_empty(old, ec) && !ec) std::filesystem::remove(old, ec);
    if (moved > 0) log::info("layout: {} UI settings files moved from {} to {}", moved, old.string(), now.string());
    return moved;
}

} // namespace nmsc
