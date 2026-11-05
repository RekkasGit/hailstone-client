#include "core/atomic_file.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <system_error>
#include <thread>

#include "core/log.h"

namespace nmsc {

bool write_file_atomic(const std::filesystem::path& path, std::string_view text) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    // A name no other writer (another thread, a second client) picks at the same time.
    static std::atomic<unsigned> serial{0};
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path tmp = path;
    tmp += ".tmp-" + std::to_string(stamp) + "-" + std::to_string(serial++);
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            log::warn("could not save {}: cannot write in its folder", path.string());
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            out.close();
            fs::remove(tmp, ec);
            log::warn("could not save {}: the write failed (disk full?)", path.string());
            return false;
        }
    }
    // std::filesystem::rename replaces an existing file (MoveFileExW with MOVEFILE_REPLACE_EXISTING on Windows).
    fs::rename(tmp, path, ec);
#ifdef _WIN32
    // Windows refuses the replace while another program has the file open without delete sharing (a second client
    // reading it, an editor, a virus scanner), which is usually over in a moment: a few more tries.
    for (int attempt = 0; ec && attempt < 5; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20 << attempt));
        ec.clear();
        fs::rename(tmp, path, ec);
    }
#endif
    if (ec) {
        log::warn("could not save {}: {}", path.string(), ec.message());
        std::error_code rm_ec;
        fs::remove(tmp, rm_ec);
        return false;
    }
    return true;
}

std::optional<std::string> read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream s;
    s << in.rdbuf();
    return std::move(s).str();
}

} // namespace nmsc
