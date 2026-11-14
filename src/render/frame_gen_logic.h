#pragma once

// The decisions of frame generation (docs/graphics.md "Frame generation"), apart from the GPU work
// (render/frame_gen.h) so they can be tested: when a generated frame can be made, how many swapchain images it needs
// and how it is presented.

#include <cstdint>

namespace hailstone::render {

// The in-between frame's place in time: halfway from the last real frame (0) to this one (1).
inline constexpr float kFrameGenTime = 0.5f;

// What a frame knows when it decides whether to generate the frame before it.
struct FrameGenState {
    bool enabled = false;       // display.frame_gen, with a temporal upscaler (the motion comes from it)
    bool temporal_frame = false; // this frame ran the temporal upscaler (its motion and depth are this frame's)
    bool history_kept = false;  // and its history went on from the last frame's (no cut, no reset, no resize)
    bool previous_ready = false; // the last frame left its picture without the UI, at this frame's size
};

// A generated frame is made only between two real frames in a row of the same scene: the motion it follows is this
// frame's against the last one's, and a cut has none worth following (kettle-framegen shows the nearer real frame
// there; here the real frame is simply presented alone).
constexpr bool frame_gen_ready(const FrameGenState& s) {
    return s.enabled && s.temporal_frame && s.history_kept && s.previous_ready;
}

// The swapchain's image count for a surface's limits (`max_count` 0: none): one more than the minimum, as before, and
// one more again with frame generation, which holds two images per real frame (the generated one and the real one).
// With min + 2 images an application may wait without a timeout for its second (the Vulkan rule: at most
// count - minImageCount images held at once).
constexpr std::uint32_t swapchain_image_count(std::uint32_t min_count, std::uint32_t max_count, bool frame_gen) {
    std::uint32_t n = min_count + 1 + (frame_gen ? 1u : 0u);
    if (max_count != 0 && n > max_count) n = max_count;
    return n;
}

// Frame generation can wait for its second image without a timeout only when the swapchain holds enough of them.
constexpr bool frame_gen_fits(std::uint32_t image_count, std::uint32_t min_count) {
    return image_count >= min_count + 2;
}

} // namespace hailstone::render
