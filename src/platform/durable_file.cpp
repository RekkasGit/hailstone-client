#include "platform/durable_file.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace hailstone::platform {

bool write_file_synced(const std::filesystem::path& path, std::string_view text) {
#if defined(_WIN32)
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    std::size_t done = 0;
    while (ok && done < text.size()) {
        const std::size_t left = text.size() - done;
        const DWORD chunk = left > 0x40000000u ? 0x40000000u : static_cast<DWORD>(left);
        DWORD wrote = 0;
        ok = ::WriteFile(h, text.data() + done, chunk, &wrote, nullptr) != 0 && wrote > 0;
        done += wrote;
    }
    if (ok) ok = ::FlushFileBuffers(h) != 0;
    if (::CloseHandle(h) == 0) ok = false;
    return ok;
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) return false;
    bool ok = true;
    std::size_t done = 0;
    while (done < text.size()) {
        const ssize_t n = ::write(fd, text.data() + done, text.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            ok = false;
            break;
        }
        done += static_cast<std::size_t>(n);
    }
#if defined(__APPLE__)
    // fsync on macOS hands the bytes to the drive, which may still hold them in its cache; F_FULLFSYNC asks it to
    // write them out. Not every file system has it, so fsync is the fallback.
    if (ok && ::fcntl(fd, F_FULLFSYNC) != 0) ok = ::fsync(fd) == 0;
#else
    if (ok) ok = ::fsync(fd) == 0;
#endif
    if (::close(fd) != 0) ok = false;
    return ok;
#endif
}

bool replace_file_synced(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec) {
    ec.clear();
#if defined(_WIN32)
    if (!::MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ec = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
        return false;
    }
    return true;
#else
    if (::rename(from.c_str(), to.c_str()) != 0) {
        ec = std::error_code(errno, std::generic_category());
        return false;
    }
    // The rename is an entry of the folder: syncing the folder puts it on the disk.
    const std::filesystem::path dir = to.has_parent_path() ? to.parent_path() : std::filesystem::path(".");
    const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0) {
        (void)::fsync(fd);
        ::close(fd);
    }
    return true;
#endif
}

} // namespace hailstone::platform
