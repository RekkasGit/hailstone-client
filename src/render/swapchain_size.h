#pragma once

// When the swapchain is made again for a new size (render/vk_context.cpp begin_frame_commands).
//
// A swapchain is made for the window's framebuffer size, but where the surface reports a defined currentExtent its
// images take that instead. The two can differ for good (some scaled or tiling desktops): comparing them would make
// the swapchain again every frame. They can also differ for a moment while a resize settles (the surface still at
// the old size): the swapchain is then made at the old size, and a check of the window alone never makes it again
// once the surface catches up. So: the window size is compared with the size the swapchain was asked for, and for a
// while after a swapchain was made with the surface and the window disagreeing, the surface's currentExtent is
// compared with the swapchain's own extent (the size it has, so a lasting difference makes it at most once).

#include <cstdint>
#include <optional>

namespace nmsc::render {

struct Extent {
    std::uint32_t width = 0, height = 0;
    bool operator==(const Extent&) const = default;
};

// Frames to keep watching the surface after a swapchain was made with its currentExtent not the window's size.
inline constexpr std::uint32_t kSurfaceWatchFrames = 120;

// How many frames to watch the surface for after making a swapchain: `surface` the currentExtent it was made with
// (none when the surface leaves the size to the swapchain), `window` the window size it was asked for.
inline std::uint32_t surface_watch_frames(std::optional<Extent> surface, Extent window) {
    return surface && *surface != window ? kSurfaceWatchFrames : 0;
}

// Whether to make the swapchain again: the window's size is not the one the swapchain was asked for, or (while the
// surface is watched: `surface` given) the surface's defined currentExtent is not the swapchain's extent.
inline bool swapchain_outdated(Extent window, Extent requested, std::optional<Extent> surface, Extent swapchain) {
    if (window != requested) return true;
    return surface && *surface != swapchain;
}

} // namespace nmsc::render
