// core/wipe.h: a password's bytes are overwritten before the string lets them go, a short one's in the string's own
// storage too.
#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "core/wipe.h"

using hailstone::core::wipe;

namespace {
// Whether the buffer at p holds no byte of what it held, up to `capacity` (the string keeps its buffer: wipe never
// shrinks it, so p still points into memory the string owns).
bool zeroed(const char* p, std::size_t capacity) {
    for (std::size_t i = 0; i < capacity; ++i)
        if (p[i] != '\0') return false;
    return true;
}
} // namespace

TEST_CASE("wipe: a long password on the heap and a short one in the string's own storage") {
    for (std::string_view text : {std::string_view("hunter2"), std::string_view("a passphrase far longer than any SSO")}) {
        CAPTURE(text);
        std::string s(text);
        const char* p = s.data();
        const std::size_t capacity = s.capacity();
        wipe(s);
        CHECK(s.empty());
        CHECK(s.data() == p);
        CHECK(s.capacity() == capacity);
        CHECK(zeroed(p, capacity));
    }
}

TEST_CASE("wipe: what a longer earlier value left beyond the end goes too") {
    std::string s(40, 'x');
    s = "pw"; // the buffer keeps the x's after "pw\0"
    const char* p = s.data();
    const std::size_t capacity = s.capacity();
    wipe(s);
    CHECK(zeroed(p, capacity));
    std::string empty;
    wipe(empty); // nothing to do, and no harm
    CHECK(empty.empty());
}
