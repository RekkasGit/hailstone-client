#pragma once

// The original's 32-bit arithmetic where a product wraps before it is divided: `imul` keeps the low 32 bits, then
// `idiv` divides them as a signed number. In C++20 `static_cast<std::int32_t>(a * b) / k` with unsigned a and b says
// exactly that, but MSVC's optimiser (19.44, /O2) folds the multiply and the divide as if nothing wrapped:
// (int32)(0x40000000u * 0x51cu) / 0x51c gives 0x40000000 instead of 0, and a focus's cast-time share of INT_MIN came
// out with its sign flipped. The recorded vectors (Social_Stop, GetFocusCastingTimeModifier) failed only in the MSVC
// build. Here the wrap is taken explicitly and the division done in 64 bits, which no compiler folds away.

#include <cstdint>

namespace hailstone::core {

// The signed 32-bit value of the low 32 bits of a result, as a 64-bit number.
constexpr std::int64_t wrapped_value(std::uint32_t low) {
    return low >= 0x80000000u ? static_cast<std::int64_t>(low) - 0x100000000LL : static_cast<std::int64_t>(low);
}

// `low` read as a signed 32-bit number, divided by `divisor` (truncating towards zero, as idiv). `divisor` is never
// 0 or -1 at the callers (constants and checked values), where idiv would fault.
constexpr std::int32_t wrapped_div(std::uint32_t low, std::int32_t divisor) {
    return static_cast<std::int32_t>(wrapped_value(low) / divisor);
}

// The imul / idiv pair: a * b keeping the low 32 bits, then that signed value divided by `divisor`. A helper that
// returns static_cast<std::int32_t>(a * b) does not help once inlined: `mul(a, b) / k` is the pattern above.
constexpr std::int32_t wrapped_mul_div(std::int32_t a, std::int32_t b, std::int32_t divisor) {
    return wrapped_div(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b), divisor);
}

} // namespace hailstone::core
