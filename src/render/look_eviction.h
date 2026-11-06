#pragma once

// When ActorRenderer may free an appearance it dressed during the zone (its Looks cache): a spawn coming back, or a
// crowd in the same gear, reuses a look at once, but a hub's arrivals, departures and illusions would otherwise keep
// every look's mesh and GPU buffers until the zone changes. Kept apart from Vulkan so the rule is tested.

#include <cstdint>

namespace nmsc::render {

// How long a look no spawn wears stays (display clock), and how often the cache is swept.
inline constexpr std::uint32_t kLookIdleMs = 5 * 60 * 1000;
inline constexpr std::uint32_t kLookSweepMs = 1000;

// Whether a look last drawn (or put up) in frame `last_frame` at `last_ms` may go now: idle for kLookIdleMs, and
// at least `frames_in_flight` frames begun since, so no frame still on the GPU reads its buffers. The clock is
// compared with unsigned wrap-around.
constexpr bool look_idle(std::uint64_t last_frame, std::uint32_t last_ms, std::uint64_t now_frame,
                         std::uint32_t now_ms, std::uint32_t frames_in_flight) {
    return now_frame >= last_frame + frames_in_flight && static_cast<std::uint32_t>(now_ms - last_ms) >= kLookIdleMs;
}

} // namespace nmsc::render
