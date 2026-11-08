#include "core/old_ui_settings.h"

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#include "core/atomic_file.h"
#include "core/log.h"
#include "platform/paths.h"

namespace hailstone {

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

std::string rename_old_window_ids(std::string_view ini) {
    constexpr std::string_view kOld = "##nmsc", kNew = "##hailstone", kWindow = "[Window][";
    std::string out(ini);
    if (out.find(kNew) != std::string::npos) return out;
    std::size_t line = 0;
    while (line < out.size()) {
        std::size_t end = out.find('\n', line);
        if (end == std::string::npos) end = out.size();
        if (std::string_view(out).substr(line, end - line).starts_with(kWindow)) {
            for (std::size_t at = out.find(kOld, line); at != std::string::npos && at < end; at = out.find(kOld, at)) {
                out.replace(at, kOld.size(), kNew);
                at += kNew.size();
                end += kNew.size() - kOld.size();
            }
        }
        line = end + 1;
    }
    return out;
}

bool rename_old_window_ids_in(const std::filesystem::path& ini_file) {
    const std::optional<std::string> text = read_text_file(ini_file);
    if (!text) return false;
    const std::string renamed = rename_old_window_ids(*text);
    if (renamed == *text) return false;
    if (!write_file_atomic(ini_file, renamed)) return false;
    log::info("layout: the window names in {} renamed (##nmsc to ##hailstone)", ini_file.string());
    return true;
}

} // namespace hailstone
