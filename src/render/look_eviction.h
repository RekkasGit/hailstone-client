#pragma once

// When ActorRenderer may free an appearance it dressed during the zone (its Looks cache): a spawn coming back, or a
// crowd in the same gear, reuses a look at once, but a hub's arrivals, departures and illusions would otherwise keep
// every look's mesh and GPU buffers until the zone changes. Kept apart from Vulkan so the rule is tested.

#include <cstdint>
#include <initializer_list>
#include <unordered_set>

namespace hailstone::render {

// How long a look no spawn wears stays (display clock), and how often the cache is swept.
inline constexpr std::uint32_t kLookIdleMs = 5 * 60 * 1000;
inline constexpr std::uint32_t kLookSweepMs = 1000;

// Whether a look last drawn (or put up) in frame `last_frame` at `last_ms` may go now: idle for kLookIdleMs, and
// at least `frames_in_flight` frames begun since. This is a cache-residency
// grace period; actual destruction still waits on VkContext's GPU completion point.
// The clock is compared with unsigned wrap-around.
constexpr bool look_idle(std::uint64_t last_frame, std::uint32_t last_ms, std::uint64_t now_frame,
                         std::uint32_t now_ms, std::uint32_t frames_in_flight) {
    return now_frame >= last_frame + frames_in_flight && static_cast<std::uint32_t>(now_ms - last_ms) >= kLookIdleMs;
}

// A spawn's state across frames (ActorRenderer::SpawnState: `model`, `mesh`, `seen`, `hidden`). A spawn hidden on
// purpose (the player in first person, a market-hidden one) keeps its state, and so the look it wears; otherwise it
// is as if it were gone.

// The spawn is hidden on purpose this frame: its state stays (seen) and is flagged hidden. True on the first such
// frame, when the caller drops the dressing still pending for it.
template <class State> bool hide_spawn_state(State& st) {
    st.seen = true;
    if (st.hidden) return false;
    st.hidden = true;
    return true;
}

// The spawn is drawn this frame with `model`: its state starts afresh when the model changed or it was hidden.
template <class State, class Model> bool spawn_state_fresh(const State& st, const Model& model) {
    return st.model != model || st.hidden;
}

// The meshes the states wear, a hidden spawn's included: a look worn by any of them is never freed.
template <class States> auto worn_meshes(const States& states) {
    std::unordered_set<decltype(states.begin()->second.mesh.get())> worn;
    for (const auto& kv : states)
        if (kv.second.mesh) worn.insert(kv.second.mesh.get());
    return worn;
}

// A texture stays while any GPU model reads it: base and tint layers and both
// material maps count, including defaults and looks retained for a hidden spawn.
// Rebuilding this set on the idle sweep avoids a separate count that can drift
// when models are adopted from the preceding zone or an upload fails midway.
template <class Models> std::unordered_set<std::uint32_t> model_texture_slots(const Models& models) {
    std::unordered_set<std::uint32_t> used{0}; // the white fallback is permanent
    for (const auto& kv : models)
        for (const auto& part : kv.second.parts)
            for (const std::uint32_t slot : {part.texture, part.texture2, part.normal_map, part.rough_map})
                if (slot != 0xffffffffu) used.insert(slot);
    return used;
}

// Array exhaustion is temporary, unlike an absent image. Once a slot is free,
// discard only those GPU models that baked in a capacity fallback and release
// those texture claims. Their retained CPU meshes can then upload again.
template <class Models, class Slots, class Retire, class Release>
bool retry_capacity_blocked_textures(Models& models, Slots& slots, bool capacity_available,
                                    Retire&& retire, Release&& release) {
    if (!capacity_available) return false;
    bool retried = false;
    for (auto it = models.begin(); it != models.end();) {
        if (!it->second.capacity_blocked) {
            ++it;
            continue;
        }
        retire(it->second);
        it = models.erase(it);
        retried = true;
    }
    for (auto it = slots.begin(); it != slots.end();) {
        if (!it->second.capacity_blocked) {
            ++it;
            continue;
        }
        release(it->first);
        it = slots.erase(it);
    }
    return retried;
}

} // namespace hailstone::render
