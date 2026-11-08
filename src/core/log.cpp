#include "core/log.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#include "platform/process.h"

namespace hailstone::log {

namespace {
std::atomic<Level> g_level{Level::Info};
std::mutex g_mutex;
std::FILE* g_file = nullptr; // guarded by g_mutex
std::filesystem::path g_path; // the file g_file writes

// The last lines, for a crash report: written under g_mutex, read without it.
constexpr std::size_t kRecentLines = 16;
constexpr std::size_t kRecentLength = 200;
char g_recent[kRecentLines][kRecentLength] = {};
std::atomic<std::size_t> g_recent_next{0}; // the slot the next line goes to (counts up; modulo kRecentLines)

void remember_line(const char* tag, std::string_view message) {
    std::size_t n = g_recent_next.load(std::memory_order_relaxed);
    char* line = g_recent[n % kRecentLines];
    std::snprintf(line, kRecentLength, "[%s] %.*s", tag, static_cast<int>(message.size()), message.data());
    g_recent_next.store(n + 1, std::memory_order_release);
}

// <name>.<n><ext>: hailstone.log -> hailstone.1.log.
std::filesystem::path numbered(const std::filesystem::path& path, const std::string& n) {
    std::filesystem::path p = path;
    p.replace_extension("." + n + path.extension().string());
    return p;
}

// The lock beside a log file (<name>.lock), held while the process runs: the program holding it owns the file. A
// second client (or a viewer) started meanwhile finds it taken and writes a log of its own.
bool lock_log(const std::filesystem::path& path) {
    static std::vector<std::filesystem::path> held; // guarded by g_mutex; this process's locks
    std::filesystem::path lock = path;
    lock.replace_extension(".lock");
    if (std::find(held.begin(), held.end(), lock) != held.end()) return true;
    held.push_back(lock); // undone below when the lock is taken by another program
    if (platform::hold_lock_file(lock)) return true; // (a lock that cannot be made at all counts as held)
    held.pop_back();
    return false;
}

const char* tag(Level level) {
    switch (level) {
    case Level::Debug: return "debug";
    case Level::Info: return "info";
    case Level::Warn: return "warn";
    case Level::Error: return "error";
    }
    return "?";
}
} // namespace

void set_level(Level level) { g_level = level; }
bool enabled(Level level) { return level >= g_level.load(); }

bool open_file(const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    std::lock_guard lock(g_mutex);
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path target = path;
    bool own = lock_log(path);
    if (own && fs::exists(path, ec)) {
        // The earlier runs' files: hailstone.log -> .1 -> .2 -> .3 (the oldest goes), so a crash's log outlives a
        // few starts after it.
        constexpr int kKept = 3;
        for (int n = kKept - 1; n >= 1; --n) {
            std::error_code rename_ec;
            if (fs::exists(numbered(path, std::to_string(n)), rename_ec))
                fs::rename(numbered(path, std::to_string(n)), numbered(path, std::to_string(n + 1)), rename_ec);
        }
        fs::rename(path, numbered(path, "1"), ec);
        if (ec) own = false; // still open somewhere (Windows): leave it alone
    }
    if (!own) target = numbered(path, std::to_string(platform::process_id())); // hailstone.<pid>.log
    g_file = platform::open_c_file(target, "wb");
    if (g_file) g_path = target;
    return g_file != nullptr;
}

std::filesystem::path file_path() {
    std::lock_guard lock(g_mutex);
    return g_file ? g_path : std::filesystem::path();
}

void close_file() {
    std::lock_guard lock(g_mutex);
    if (g_file) std::fclose(g_file);
    g_file = nullptr;
}

std::size_t copy_recent_lines(char* out, std::size_t size) {
    if (size == 0) return 0;
    std::size_t at = 0;
    const std::size_t next = g_recent_next.load(std::memory_order_acquire);
    const std::size_t first = next > kRecentLines ? next - kRecentLines : 0;
    for (std::size_t i = first; i < next; ++i) {
        const char* line = g_recent[i % kRecentLines];
        for (std::size_t k = 0; k < kRecentLength && line[k] && at + 1 < size; ++k) out[at++] = line[k];
        if (at + 1 < size) out[at++] = '\n';
    }
    out[at] = '\0';
    return at;
}

void write(Level level, std::string_view message) {
    if (level < g_level.load()) return;
    std::lock_guard lock(g_mutex);
    remember_line(tag(level), message);
    std::fprintf(level >= Level::Warn ? stderr : stdout, "[%s] %.*s\n", tag(level),
                 static_cast<int>(message.size()), message.data());
    if (g_file) {
        // Flushed at once, so the file holds everything up to a crash.
        std::fprintf(g_file, "[%s] %.*s\n", tag(level), static_cast<int>(message.size()), message.data());
        std::fflush(g_file);
    }
}

} // namespace hailstone::log
