#pragma once

// The maths of the temporal upscaler (docs/graphics.md "Temporal upscaling"): the sub-pixel jitter put on the
// scene's projection, the reprojection from this frame's view to the last one's that the motion vectors come from,
// and when the history is thrown away. No Vulkan here: render/vk_context.cpp runs the passes.

#include <cstdint>
#include <string_view>

#include <glm/glm.hpp>

namespace hailstone::render {

// How the 3D view is brought up to its size (display.upscaler). Fsr1: spatial (FSR 1's EASU + RCAS) below a render
// scale of 100, nothing at 100, MSAA as display.msaa says. Taa: temporal, the scene jittered each frame and
// accumulated into a history at the view's size (shaders/taa_resolve.frag), at every render scale; MSAA is off.
// Fsr3: the same jittered frames and motion, resolved by AMD FSR 3.1's upscaler (render/fsr3_upscaler.h); Taa where
// the device lacks what it needs.
enum class Upscaler : std::uint8_t { Fsr1, Taa, Fsr3 };

// display.upscaler's values: "fsr1", "taa", "fsr3"; anything else is Fsr1.
Upscaler parse_upscaler(std::string_view name);
std::string_view upscaler_name(Upscaler u);

// The radical inverse of `index` in `base` (the Halton sequence): 1 -> 1/2, 2 -> 1/4, 3 -> 3/4 in base 2.
float halton(std::uint32_t index, std::uint32_t base);

// How many jitter positions a cycle holds at render scale `scale` (0.5..1 of the view on each side): 8 at 1, more as
// each scene pixel covers more of the view's, so every view pixel still sees a sample near it (FSR 2's
// ffxFsr2GetJitterPhaseCount, 8 x the area ratio, rounded up).
std::uint32_t jitter_phases(float scale);

// Frame `frame`'s jitter in scene pixels, each axis in -0.5..0.5 (x right, y down): Halton (2, 3) from its second
// point on, repeating every `phases` frames. All geometry is drawn shifted by it.
glm::vec2 jitter_offset(std::uint64_t frame, std::uint32_t phases);

// That jitter as an offset in normalised device coordinates for a target of `width` x `height` pixels. The scene's
// viewport is flipped (vk_context.cpp begin_scene_rendering): NDC y is up, pixel y down.
glm::vec2 jitter_ndc(glm::vec2 jitter_px, std::uint32_t width, std::uint32_t height);

// `proj` with its output moved by `ndc` (an NDC offset): a perspective or orthographic projection alike.
glm::mat4 jittered(const glm::mat4& proj, glm::vec2 ndc);

// The matrix from this frame's clip space (unjittered, `view` and `proj`) to the last frame's: a point's NDC and
// depth in, its place a frame ago out (divide by w). Worked in double precision and through the cameras' relative
// motion, so world coordinates in the tens of thousands keep the sub-pixel precision the history needs.
glm::mat4 reprojection(const glm::mat4& prev_view, const glm::mat4& prev_proj, const glm::mat4& view,
                       const glm::mat4& proj);

// A jump of the camera between two frames that the history cannot follow (a teleport, a camera switch, a cut):
// it moved more than `max_step` world units, or turned more than `max_turn` radians.
inline constexpr float kHistoryMaxStep = 25.0f;
inline constexpr float kHistoryMaxTurn = 0.5f;
bool camera_cut(const glm::mat4& prev_view, const glm::mat4& view, float max_step = kHistoryMaxStep,
                float max_turn = kHistoryMaxTurn);

} // namespace hailstone::render
