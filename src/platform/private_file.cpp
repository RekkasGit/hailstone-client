#include "platform/private_file.h"

#include <system_error>

#if defined(_WIN32)
#include <fstream>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace nmsc::platform {

bool write_private_file(const std::filesystem::path& path, std::string_view text) {
    bool ok = false;
#if defined(_WIN32)
    std::error_code ec;
    if (std::filesystem::exists(path, ec) || ec) return false;
    {
        std::ofstream out(path, std::ios::binary);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        ok = static_cast<bool>(out);
    }
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (fd < 0) return false;
    ok = true;
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
    if (::close(fd) != 0) ok = false;
#endif
    if (!ok) {
        std::error_code rm;
        std::filesystem::remove(path, rm);
    }
    return ok;
}

bool make_private_dir(const std::filesystem::path& path) {
#if defined(_WIN32)
    std::error_code ec;
    return std::filesystem::create_directory(path, ec) && !ec;
#else
    return ::mkdir(path.c_str(), S_IRWXU) == 0;
#endif
}

} // namespace nmsc::platform
