#include "platform/local_time.h"

namespace nmsc::platform {

std::optional<std::tm> local_time(std::time_t t) {
    std::tm out{};
#if defined(_WIN32)
    if (localtime_s(&out, &t) != 0) return std::nullopt;
#else
    if (!localtime_r(&t, &out)) return std::nullopt;
#endif
    return out;
}

} // namespace nmsc::platform
