#include "platform/open_folder.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_misc.h>

#include <cctype>
#include <system_error>

#include "core/log.h"

namespace hailstone::platform {

std::string file_url(const std::filesystem::path& path) {
    std::u8string u8 = path.generic_u8string();
    std::string p(u8.begin(), u8.end());
    std::string url = "file://";
    if (p.empty() || p.front() != '/') url += '/';
    constexpr char kHex[] = "0123456789ABCDEF";
    for (unsigned char c : p) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/' || c == ':') {
            url += static_cast<char>(c);
        } else {
            url += '%';
            url += kHex[c >> 4];
            url += kHex[c & 15];
        }
    }
    return url;
}

bool open_folder(const std::filesystem::path& folder) {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        log::warn("could not create {}: {}", folder.string(), ec.message());
        return false;
    }
    std::filesystem::path absolute = std::filesystem::absolute(folder, ec);
    if (ec) absolute = folder;
    if (!SDL_OpenURL(file_url(absolute).c_str())) {
        log::warn("could not open {}: {}", absolute.string(), SDL_GetError());
        return false;
    }
    return true;
}

bool open_url(const std::string& url) {
    if (SDL_OpenURL(url.c_str())) return true;
    log::warn("could not open {}: {}", url, SDL_GetError());
    return false;
}

} // namespace hailstone::platform
