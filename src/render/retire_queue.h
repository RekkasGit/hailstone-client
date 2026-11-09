#pragma once

// When the GPU is done with what the CPU let go of (render::VkContext::retire, its uploads' staging buffers): kept
// apart from Vulkan so the rule is tested.
//
// A point in the queue's work is the frame being recorded (VkContext::frames_begun) and the last upload submitted
// (an upload serial) when something was let go: it may be destroyed once that frame and that upload are done on the
// GPU. Uploads are submitted on the frame's queue ahead of the frame's own commands; each has its fence, as a frame's
// fence covers only its own submission.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <utility>
#include <vector>

namespace hailstone::render {

struct GpuPoint {
    std::uint64_t frame = 0;  // a frame's number (1 the first; 0 none)
    std::uint64_t upload = 0; // an upload's serial (1 the first; 0 none)
};

// Whether the GPU has passed `at`, with every frame up to done.frame and every upload up to done.upload finished.
constexpr bool gpu_point_reached(GpuPoint at, GpuPoint done) {
    return at.frame <= done.frame && at.upload <= done.upload;
}

// What waits for a GpuPoint: each release runs once the point is reached (collect), or when the device is idle
// (flush). Main thread only, as Vulkan's frame loop is.
class RetireQueue {
public:
    void push(GpuPoint at, std::function<void()> release) { entries_.push_back({at, std::move(release)}); }

    // Runs, oldest first, every release whose point `done` has reached; returns how many ran. A release may push
    // more (they wait for the next collect).
    std::size_t collect(GpuPoint done) {
        std::vector<std::function<void()>> ready;
        std::size_t kept = 0;
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (gpu_point_reached(entries_[i].at, done)) {
                ready.push_back(std::move(entries_[i].release));
            } else {
                if (kept != i) entries_[kept] = std::move(entries_[i]);
                ++kept;
            }
        }
        entries_.resize(kept);
        for (auto& r : ready)
            if (r) r();
        return ready.size();
    }

    // Runs every release (the device is idle, or going); returns how many ran.
    std::size_t flush() {
        std::size_t ran = 0;
        while (!entries_.empty()) {
            std::deque<Entry> now;
            now.swap(entries_);
            for (Entry& e : now) {
                if (e.release) e.release();
                ++ran;
            }
        }
        return ran;
    }

    std::size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }

private:
    struct Entry {
        GpuPoint at;
        std::function<void()> release;
    };
    std::deque<Entry> entries_;
};

// The uploads submitted and not known to be done, oldest first, each with what it holds (`Handle`: its fence and
// command buffer) and the staging bytes it keeps alive. Serials count from 1.
template <class Handle> class UploadTracker {
public:
    std::uint64_t submitted(Handle handle, std::uint64_t bytes) {
        pending_.push_back({++last_, handle, bytes});
        bytes_ += bytes;
        return last_;
    }

    // Takes off, oldest first, the uploads `done(handle)` says are finished, handing each handle to `recycle`; stops
    // at the first one not finished (a later one is counted done only when every earlier one is).
    template <class Done, class Recycle> void poll(Done&& done, Recycle&& recycle) {
        while (!pending_.empty() && done(pending_.front().handle)) pop(recycle);
    }

    // Waits for the oldest (`wait(handle)`) and takes it off; false when none is pending.
    template <class Wait, class Recycle> bool wait_oldest(Wait&& wait, Recycle&& recycle) {
        if (pending_.empty()) return false;
        wait(pending_.front().handle);
        pop(recycle);
        return true;
    }

    // Every upload is done (the device was waited for): each handle goes to `recycle`.
    template <class Recycle> void all_done(Recycle&& recycle) {
        while (!pending_.empty()) pop(recycle);
    }

    // The last serial whose upload and every earlier one are done.
    std::uint64_t completed() const { return pending_.empty() ? last_ : pending_.front().serial - 1; }
    std::uint64_t last() const { return last_; }
    std::size_t pending() const { return pending_.size(); }
    std::uint64_t bytes_in_flight() const { return bytes_; }

private:
    struct Entry {
        std::uint64_t serial;
        Handle handle;
        std::uint64_t bytes;
    };
    template <class Recycle> void pop(Recycle& recycle) {
        Entry e = std::move(pending_.front());
        pending_.pop_front();
        bytes_ -= e.bytes;
        recycle(e.handle);
    }
    std::deque<Entry> pending_;
    std::uint64_t last_ = 0;
    std::uint64_t bytes_ = 0;
};

// The last frame known done on the GPU once frame `begun` (VkContext::frames_begun, the one about to be recorded) has
// waited for its slot's fence, with `in_flight` slots: that fence was the frame `in_flight` before it's.
constexpr std::uint64_t frames_done_at(std::uint64_t begun, std::uint32_t in_flight) {
    return begun > in_flight ? begun - in_flight : 0;
}

} // namespace hailstone::render
