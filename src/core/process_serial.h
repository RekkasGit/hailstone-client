#pragma once

// A number unique in the process (from 1), for "changed since I last looked" serials that must also differ between
// two objects: a client that keeps the serial it last saw notices a new object's first change too, where a serial
// counted per object would start again at the same number.

#include <atomic>
#include <cstdint>

namespace hailstone::core {

inline std::uint32_t next_process_serial() {
    static std::atomic<std::uint32_t> counter{0};
    std::uint32_t n = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n == 0) n = counter.fetch_add(1, std::memory_order_relaxed) + 1; // (0 means none: skipped on the wrap)
    return n;
}

} // namespace hailstone::core
