// render/swapchain_size.h: when the swapchain is made again for a new size.

#include <doctest/doctest.h>

#include "render/swapchain_size.h"

using namespace hailstone::render;

TEST_CASE("Swapchain size: a surface that lags a resize is caught up; one that differs for good is not chased") {
    const Extent old_size{800, 600}, new_size{1024, 768};
    // The window changed: made again.
    CHECK(swapchain_outdated(new_size, old_size, std::nullopt, old_size));
    // Made for 1024x768 while the surface still said 800x600: the surface is watched.
    CHECK(surface_watch_frames(old_size, new_size) == kSurfaceWatchFrames);
    CHECK_FALSE(swapchain_outdated(new_size, new_size, old_size, old_size)); // still lagging
    CHECK(swapchain_outdated(new_size, new_size, new_size, old_size));       // caught up: made again
    // Made again at the surface's new size, which is the window's: nothing more to watch.
    CHECK(surface_watch_frames(new_size, new_size) == 0);
    // A surface left to the swapchain (no currentExtent): nothing to watch.
    CHECK(surface_watch_frames(std::nullopt, new_size) == 0);
    // A surface that keeps another size for good: once made at it, the comparison with the swapchain's own extent
    // stays equal, so it is not made again every frame (and the watch runs out).
    const Extent scaled{2048, 1536};
    CHECK(surface_watch_frames(scaled, new_size) == kSurfaceWatchFrames);
    CHECK_FALSE(swapchain_outdated(new_size, new_size, scaled, scaled));
    CHECK_FALSE(swapchain_outdated(new_size, new_size, std::nullopt, scaled));
}
