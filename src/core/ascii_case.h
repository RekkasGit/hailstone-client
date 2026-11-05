#pragma once

// ASCII case-insensitive comparison and file-name matching, in place of POSIX strcasecmp / strncasecmp and fnmatch
// (none of which Windows has). Only 'A'-'Z' fold, as strcasecmp does in the "C" locale; other bytes compare as
// unsigned values.

#include <cstddef>
#include <string_view>

namespace nmsc {

inline char ascii_lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

// Less than, equal to or greater than zero as a sorts before, equal to or after b, ignoring case (strcasecmp).
inline int icompare(std::string_view a, std::string_view b) {
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto x = static_cast<unsigned char>(ascii_lower(a[i]));
        const auto y = static_cast<unsigned char>(ascii_lower(b[i]));
        if (x != y) return x < y ? -1 : 1;
    }
    if (a.size() == b.size()) return 0;
    return a.size() < b.size() ? -1 : 1;
}

inline bool iequals(std::string_view a, std::string_view b) { return icompare(a, b) == 0; }

inline bool istarts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

inline bool iends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && iequals(s.substr(s.size() - suffix.size()), suffix);
}

// A file-name wildcard, ignoring case: '*' matches any run of characters, '?' any one; nothing else is special
// (the Win32 FindFirstFile wildcards, not fnmatch's [...] classes or '\' escapes).
inline bool iglob(std::string_view pattern, std::string_view name) {
    std::size_t p = 0, n = 0;
    std::size_t star = std::string_view::npos, resume = 0;
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || ascii_lower(pattern[p]) == ascii_lower(name[n]))) {
            ++p;
            ++n;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            resume = n;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            n = ++resume;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

} // namespace nmsc
