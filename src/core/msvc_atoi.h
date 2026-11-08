#pragma once

// The original's atoi / atol (MSVC's, with its 32-bit long): leading white space, a sign, then digits (the rest
// ignored; none: 0). The digits accumulate in 32 bits and wrap, so the result is the same on every platform and free
// of std::atoi's undefined behaviour on overflow. Whether the original's CRT wraps or saturates an out-of-range
// number is open (docs/unknowns.md).

#include <cstdint>
#include <string_view>

namespace hailstone::core {

inline std::int32_t msvc_atoi(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || (s[i] >= '\t' && s[i] <= '\r'))) ++i;
    bool negative = false;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) negative = s[i++] == '-';
    std::uint32_t v = 0;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) v = v * 10u + static_cast<std::uint32_t>(s[i] - '0');
    return static_cast<std::int32_t>(negative ? 0u - v : v);
}

} // namespace hailstone::core
