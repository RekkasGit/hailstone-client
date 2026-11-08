#pragma once

// File names Windows does not keep as written. Whatever writes a name it was given (an archive's entry, a server's
// file list) refuses these on every platform, so the same name means the same file everywhere:
// - a part ending in '.' or ' ': Windows drops the trailing dots and spaces, so "evil.exe." is written as evil.exe;
// - a device name (CON, PRN, AUX, NUL, CONIN$, CONOUT$, COM1-COM9, LPT1-LPT9, and COM / LPT with a superscript
//   1, 2 or 3), in any case and with any extension ("con.txt", "Lpt1.dds", "nul .txt"): Windows opens the device.

#include <string>
#include <string_view>

#include "core/ascii_case.h"

namespace hailstone {

// Whether one path part (no separators) names a Windows device.
inline bool is_windows_device_name(std::string_view part) {
    std::string stem;
    for (char c : part.substr(0, part.find('.'))) stem += ascii_lower(c);
    while (!stem.empty() && stem.back() == ' ') stem.pop_back(); // Windows matches "con .txt" too
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" || stem == "conin$" || stem == "conout$")
        return true;
    if (!stem.starts_with("com") && !stem.starts_with("lpt")) return false;
    const std::string_view digit = std::string_view(stem).substr(3);
    if (digit.size() == 1) return digit[0] >= '1' && digit[0] <= '9';
    // UTF-8 superscript one, two and three (U+00B9, U+00B2, U+00B3), which Windows also takes as the digits.
    return digit == "\xc2\xb9" || digit == "\xc2\xb2" || digit == "\xc2\xb3";
}

// Whether Windows would store one path part under another name or open a device for it.
inline bool is_windows_aliased_part(std::string_view part) {
    if (part.empty()) return false;
    return part.back() == '.' || part.back() == ' ' || is_windows_device_name(part);
}

// The key two paths share when Windows (or a case-insensitive macOS volume) would make them one file: lower case,
// with each part's trailing dots and spaces dropped. '/' separates parts.
inline std::string windows_file_key(std::string_view path) {
    std::string out;
    std::size_t start = 0;
    while (true) {
        const std::size_t slash = path.find('/', start);
        std::string_view part = path.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        while (!part.empty() && (part.back() == '.' || part.back() == ' ')) part.remove_suffix(1);
        for (char c : part) out += ascii_lower(c);
        if (slash == std::string_view::npos) return out;
        out += '/';
        start = slash + 1;
    }
}

} // namespace hailstone
