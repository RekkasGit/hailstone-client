#pragma once

// A file's last-write time (std::filesystem::file_time_type) as calendar time. std::chrono::clock_cast does it in
// libstdc++ and MSVC's library; libc++ (Apple's) has no clock_cast, but its file_clock converts itself (to_sys).

#include <chrono>
#include <ctime>
#include <filesystem>

namespace nmsc {

inline std::chrono::system_clock::time_point file_time_to_system(std::filesystem::file_time_type t) {
#if defined(_LIBCPP_VERSION)
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        std::filesystem::file_time_type::clock::to_sys(t));
#else
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        std::chrono::clock_cast<std::chrono::system_clock>(t));
#endif
}

inline std::time_t file_time_to_time_t(std::filesystem::file_time_type t) {
    return std::chrono::system_clock::to_time_t(file_time_to_system(t));
}

} // namespace nmsc
