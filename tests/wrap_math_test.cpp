// core/wrap_math.h: a wrapped 32-bit product divided as the original's idiv divides it, in every compiler's build.
#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>

#include "core/wrap_math.h"

using nmsc::core::wrapped_div;
using nmsc::core::wrapped_mul_div;
using nmsc::core::wrapped_value;

// Checked at compile time too, so a compiler's constant folding has to agree.
static_assert(wrapped_value(0x80000000u) == -2147483648LL);
static_assert(wrapped_value(0x7fffffffu) == 2147483647LL);
static_assert(wrapped_div(0x40000000u * 0x51cu, 0x51c) == 0);
static_assert(wrapped_div(0x80000000u, -100) == 21474836);
static_assert(wrapped_mul_div(5000, 500000, 10000) == -179496);

namespace {
// From a run-time value, so nothing folds: the multiplier the tests below read.
std::uint32_t runtime(const char* text) { return static_cast<std::uint32_t>(std::strtoul(text, nullptr, 10)); }
} // namespace

TEST_CASE("wrapped_div: the product's low 32 bits, signed, divided towards zero") {
    // Social_Stop's record offset: 0x51c times an index that wraps to a multiple of 2^32 is 0, not the index.
    const std::uint32_t k = runtime("1308"); // 0x51c
    CHECK(wrapped_div(0x40000000u * k, 0x51c) == 0);
    CHECK(wrapped_div(2863311u * k, 0x51c) == -420303);
    CHECK(wrapped_div(5u * k, 0x51c) == 5);
    // A focus's cast-time share (GetFocusCastingTimeModifier): INT_MIN over -100 is positive.
    CHECK(wrapped_div(runtime("2147483648") * 95u, -100) == 21474836);
    CHECK(wrapped_div(runtime("3000") * static_cast<std::uint32_t>(-20), -100) == 600);
    // Truncation towards zero both ways, as idiv.
    CHECK(wrapped_div(static_cast<std::uint32_t>(-7), 2) == -3);
    CHECK(wrapped_div(7u, -2) == -3);
}

TEST_CASE("wrapped_mul_div: imul then idiv, for the helpers that wrapped a product and divided it") {
    const auto k = static_cast<std::int32_t>(runtime("5000"));
    // Max HP's SPA 214 share: 5000 * 500000 wraps to -1794967296 before the / 10000.
    CHECK(wrapped_mul_div(k, 500000, 10000) == -179496);
    CHECK(wrapped_mul_div(k, 100, 10000) == 50);
    CHECK(wrapped_mul_div(-k, 3, 2) == -7500);
    CHECK(wrapped_mul_div(k, 1, -1) == -5000);
}
