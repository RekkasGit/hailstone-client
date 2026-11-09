#pragma once

// Overwriting a secret (a password) before it is let go, so that neither a crash dump nor memory handed back to the
// allocator keeps it.

#include <cstddef>
#include <string>

namespace hailstone::core {

// Zeroes the string's whole buffer, up to its capacity (so what a longer earlier value left there goes too, and a
// short one's bytes in the string's own storage), then empties it. Volatile writes: the compiler may not drop them
// as dead stores before the string is freed. Copies made earlier (a moved-from string, a reallocation) are not
// reached; wipe those where they are made.
inline void wipe(std::string& s) {
    s.resize(s.capacity()); // never allocates
    volatile char* p = s.data();
    for (std::size_t i = 0; i < s.size(); ++i) p[i] = '\0';
    s.clear();
}

} // namespace hailstone::core
