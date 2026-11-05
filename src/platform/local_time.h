#pragma once

// A time in the player's own time zone, thread-safely (std::localtime shares one buffer between threads; the
// reentrant forms differ between Windows and POSIX).

#include <ctime>
#include <optional>

namespace nmsc::platform {

// `t` broken down in the local time zone; nullopt when the system cannot (a time far out of range).
std::optional<std::tm> local_time(std::time_t t);

} // namespace nmsc::platform
