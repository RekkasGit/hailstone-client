#include "render/scene_recorder.h"

#include <algorithm>

#include "core/worker_pool.h"
#include "render/vk_util.h"

namespace hailstone::render {

SceneRecorder::SceneRecorder(VkContext& vk, bool scene) : vk_(vk), scene_(scene) {}

SceneRecorder::~SceneRecorder() {
    // The pools' buffers may be pending in a frame in flight.
    vk_.wait_idle();
    for (auto& slot : pools_)
        for (auto& p : slot)
            if (p.pool) vkDestroyCommandPool(vk_.device(), p.pool, nullptr);
}

bool SceneRecorder::begin_frame(const FrameInfo& frame, bool enabled) {
    reset();
    targets_.clear();
    target_ = 0;
    slot_ = frame.frame_slot;
    core::WorkerPool* pool = core::workers();
    active_ = enabled && pool && pool->workers() > 0;
    if (active_)
        for (auto& p : pools_[slot_])
            if (p.used > 0) {
                vk_check(vkResetCommandPool(vk_.device(), p.pool, 0), "vkResetCommandPool");
                p.used = 0;
            }
    if (scene_) {
        // The scene's rendering as begin_scene begins it, asked when the jobs are recorded (after begin_scene).
        add_target({[this] { return vk_.scene_inheritance(); }, [this](VkCommandBuffer cmd) { vk_.set_scene_state(cmd); }});
        vk_.set_scene_secondary(active_);
    }
    return active_;
}

std::size_t SceneRecorder::add_target(Target target) {
    targets_.push_back(std::move(target));
    target_ = targets_.size() - 1;
    return target_;
}

void SceneRecorder::add(const FrameInfo& frame, const void* lane, Job job) {
    if (!active_) {
        job(frame);
        return;
    }
    auto it = std::find(lanes_.begin(), lanes_.end(), lane);
    std::size_t index = static_cast<std::size_t>(it - lanes_.begin());
    if (it == lanes_.end()) {
        lanes_.push_back(lane);
        by_lane_.emplace_back();
    }
    by_lane_[index].push_back(jobs_.size());
    jobs_.push_back({index, target_, std::move(job), VK_NULL_HANDLE});
}

VkCommandBuffer SceneRecorder::take(Pool& pool) {
    if (pool.used == pool.buffers.size()) {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = pool.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vk_check(vkAllocateCommandBuffers(vk_.device(), &ai, &cmd), "vkAllocateCommandBuffers (scene)");
        pool.buffers.push_back(cmd);
    }
    return pool.buffers[pool.used++];
}

void SceneRecorder::record(const FrameInfo& frame) {
    if (!active_ || jobs_.empty()) return;
    auto& pools = pools_[slot_];
    while (pools.size() < lanes_.size()) {
        VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        ci.queueFamilyIndex = vk_.queue_family();
        Pool p;
        vk_check(vkCreateCommandPool(vk_.device(), &ci, nullptr, &p.pool), "vkCreateCommandPool (scene)");
        pools.push_back(std::move(p));
    }
    // The buffers are taken here, on this thread: a pool is touched by one thread at a time.
    for (auto& q : jobs_) q.cmd = take(pools[q.lane]);
    // Each target's inheritance, asked once here; the jobs point at these while they record.
    std::vector<VkCommandBufferInheritanceRenderingInfo> renderings;
    renderings.reserve(targets_.size());
    for (const auto& t : targets_) renderings.push_back(t.rendering());
    std::vector<VkCommandBufferInheritanceInfo> inheritances(targets_.size(),
                                                             {VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO});
    for (std::size_t i = 0; i < targets_.size(); ++i) inheritances[i].pNext = &renderings[i];

    try {
        core::parallel_for(lanes_.size(), 1, [&](std::size_t begin, std::size_t end) {
            for (std::size_t lane = begin; lane < end; ++lane)
                for (std::size_t j : by_lane_[lane]) {
                    Queued& q = jobs_[j];
                    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                    bi.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT |
                               VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                    bi.pInheritanceInfo = &inheritances[q.target];
                    vk_check(vkBeginCommandBuffer(q.cmd, &bi), "vkBeginCommandBuffer (scene)");
                    targets_[q.target].state(q.cmd);
                    FrameInfo own = frame;
                    own.cmd = q.cmd;
                    q.job(own);
                    vk_check(vkEndCommandBuffer(q.cmd), "vkEndCommandBuffer (scene)");
                }
        });
    } catch (...) {
        reset(); // the buffers left recording are reset with their pool, kFramesInFlight frames on
        throw;
    }
}

void SceneRecorder::execute(const FrameInfo& frame, std::size_t target) {
    execute_.clear();
    for (const auto& q : jobs_)
        if (q.target == target && q.cmd != VK_NULL_HANDLE) execute_.push_back(q.cmd);
    if (!execute_.empty())
        vkCmdExecuteCommands(frame.cmd, static_cast<std::uint32_t>(execute_.size()), execute_.data());
}

void SceneRecorder::flush(const FrameInfo& frame) {
    if (!active_ || jobs_.empty()) return;
    record(frame);
    execute_.clear();
    for (const auto& q : jobs_) execute_.push_back(q.cmd);
    vkCmdExecuteCommands(frame.cmd, static_cast<std::uint32_t>(execute_.size()), execute_.data());
    reset();
}

void SceneRecorder::reset() {
    jobs_.clear();
    lanes_.clear();
    by_lane_.clear();
}

} // namespace hailstone::render
