// render/look_eviction.h: when an idle dressed appearance may be freed.

#include <doctest/doctest.h>

#include "render/look_eviction.h"

using nmsc::render::kLookIdleMs;
using nmsc::render::look_idle;

TEST_CASE("look_idle: idle long enough and past the frames in flight") {
    CHECK_FALSE(look_idle(100, 1000, 100, 1000, 2));               // drawn this frame
    CHECK_FALSE(look_idle(100, 1000, 5000, 1000 + kLookIdleMs - 1, 2)); // not idle long enough
    CHECK(look_idle(100, 1000, 5000, 1000 + kLookIdleMs, 2));
    // However long ago by the clock, a frame still on the GPU may read its buffers.
    CHECK_FALSE(look_idle(100, 1000, 101, 1000 + 2 * kLookIdleMs, 2));
    CHECK(look_idle(100, 1000, 102, 1000 + 2 * kLookIdleMs, 2));
    // The display clock wrapping past 2^32 ms.
    CHECK(look_idle(1, 0xffffff00u, 10, kLookIdleMs, 2));
    CHECK_FALSE(look_idle(1, 0xffffff00u, 10, 0x10u, 2));
}
