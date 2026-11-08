#pragma once

// The project's own environment variables (debugging, test drives, the tests' data): HAILSTONE_<name>, read under
// NMSC_<name> too as they were called up to 0.6, so an old shell setup keeps working. HAILSTONE_ wins when both are
// set.

#include <cstdlib>
#include <string>
#include <string_view>

namespace hailstone::core {

// `get` is std::getenv but for the tests.
template <typename Get> const char* env(std::string_view name, Get&& get) {
    const std::string now = "HAILSTONE_" + std::string(name);
    if (const char* v = get(now.c_str())) return v;
    const std::string old = "NMSC_" + std::string(name);
    return get(old.c_str());
}

inline const char* env(std::string_view name) {
    return env(name, [](const char* n) -> const char* { return std::getenv(n); });
}

} // namespace hailstone::core
