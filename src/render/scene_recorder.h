#pragma once

// The scene's draws recorded on the worker threads (docs/graphics.md "Worker threads"): each draw is a job in a lane,
// recorded into a secondary command buffer of its own; the lanes are recorded at once (core::parallel_for), a lane's
// jobs in the order they came on one thread, and the buffers are executed into the frame's command buffer in the order
// the jobs came, so what is drawn, and in what order, is as it was inline.
//
// A lane is whatever the caller names by an address: one renderer, or several whose draws read or write the same state
// (a renderer whose second draw uses its first's result, a draw that asks another renderer's mutable caches). Jobs of
// different lanes must touch nothing in common but read-only state; the caller changes nothing they read between
// add and flush.
//
// Inactive (begin_frame's `enabled` false, or no worker pool), add records the job at once into the frame's own
// command buffer and flush does nothing: the draws as they were before.

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "render/scene_lanes.h"
#include "render/vk_context.h"

namespace hailstone::render {

class SceneRecorder {
public:
    explicit SceneRecorder(VkContext& vk);
    ~SceneRecorder();
    SceneRecorder(const SceneRecorder&) = delete;
    SceneRecorder& operator=(const SceneRecorder&) = delete;

    // Starts a frame (after VkContext::begin_frame_commands, before begin_scene): whether this frame's scene is
    // recorded into secondary command buffers, which it tells the VkContext (set_scene_secondary). The frame slot's
    // buffers from kFramesInFlight frames ago are done with (its fence was waited for) and are reset.
    bool begin_frame(const FrameInfo& frame, bool enabled);
    bool active() const { return active_; }

    using Job = std::function<void(const FrameInfo& frame)>;
    // Queues `job` in `lane`, to be recorded at flush with a FrameInfo whose cmd is a secondary command buffer inside
    // the scene's rendering; inactive, runs it now on `frame`.
    void add(const FrameInfo& frame, const void* lane, Job job);
    // Records the queued jobs and executes them into frame.cmd, in the order they came. Call before the scene's
    // rendering ends (VkContext::begin_translucent, begin_overlay) and before anything the jobs read changes.
    void flush(const FrameInfo& frame);

private:
    struct Pool {
        VkCommandPool pool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> buffers;
        std::size_t used = 0;
    };
    struct Queued {
        Job job;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
    };
    VkCommandBuffer take(Pool& pool);

    VkContext& vk_;
    // One pool per frame slot and lane (in the order lanes first came this frame): a pool's buffers are recorded only
    // by the thread recording its lane.
    std::array<std::vector<Pool>, VkContext::kFramesInFlight> pools_;
    std::vector<Queued> jobs_;
    SceneLanes lanes_;                     // each job's lane
    std::vector<VkCommandBuffer> execute_; // flush's buffers, in the jobs' order
    std::uint32_t slot_ = 0;
    bool active_ = false;
};

} // namespace hailstone::render
