#pragma once

#include <cstddef>
#include <filesystem>
#include <format>
#include <string_view>

namespace hailstone::log {

enum class Level { Debug, Info, Warn, Error };

void set_level(Level level);
// Whether a line at `level` is written at the level set now.
bool enabled(Level level);
void write(Level level, std::string_view message);
// Also writes every line to `path` from now on (the client's logs/hailstone.log), as well as to stdout and stderr.
// The earlier runs' files are kept as <name>.1<ext> to <name>.3<ext> (hailstone.1.log is the last run's). A program
// that runs while another holds `path` (a second client; <name>.lock beside it is the lock) leaves it alone and
// writes <name>.<process id><ext> instead. False when the file cannot be created: the log then goes to stdout and
// stderr only.
bool open_file(const std::filesystem::path& path);
// The file the log writes to (empty when none).
std::filesystem::path file_path();
void close_file();
// The last lines written (at most 16, each cut to 199 characters), oldest first, one per line, into `out` (always
// NUL-terminated); the length written. It takes no lock and allocates nothing, so a crash handler may call it
// (platform/crash_log.h); a line written meanwhile may come out mixed.
std::size_t copy_recent_lines(char* out, std::size_t size);

// Debug lines are not formatted while the level leaves them out: the per-frame paths log a few (spawn appearances,
// animations), and formatting them cost every frame.
template <typename... Args> void debug(std::format_string<Args...> fmt, Args&&... args) {
    if (!enabled(Level::Debug)) return;
    write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args> void info(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args> void warn(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args> void error(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace hailstone::log
