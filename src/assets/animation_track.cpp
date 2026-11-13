#include "assets/character.h"

#include <algorithm>
#include <bit>
#include <unordered_map>
#include <utility>

namespace hailstone::assets {
namespace {

// Compare the components the sampler reads, not struct padding or approximate float equality. This also keeps
// signed zero and quaternion sign distinct; compaction must not change interpolation or normalization behavior.
std::array<std::uint32_t, 10> pose_words(const BonePose& p) {
    return {std::bit_cast<std::uint32_t>(p.rotation.w), std::bit_cast<std::uint32_t>(p.rotation.x),
            std::bit_cast<std::uint32_t>(p.rotation.y), std::bit_cast<std::uint32_t>(p.rotation.z),
            std::bit_cast<std::uint32_t>(p.translation.x), std::bit_cast<std::uint32_t>(p.translation.y),
            std::bit_cast<std::uint32_t>(p.translation.z), std::bit_cast<std::uint32_t>(p.scale.x),
            std::bit_cast<std::uint32_t>(p.scale.y), std::bit_cast<std::uint32_t>(p.scale.z)};
}

bool same_pose(const BonePose& a, const BonePose& b) { return pose_words(a) == pose_words(b); }

} // namespace

AnimationTrack::AnimationTrack(std::initializer_list<BonePose> keys) : count_(keys.size()) {
    if (count_) keys_ = std::make_shared<std::vector<BonePose>>(keys);
}

AnimationTrack::AnimationTrack(AnimationTrack&& other) noexcept
    : keys_(std::move(other.keys_)), count_(std::exchange(other.count_, 0)) {}

AnimationTrack& AnimationTrack::operator=(AnimationTrack&& other) noexcept {
    if (this != &other) {
        keys_ = std::move(other.keys_);
        count_ = std::exchange(other.count_, 0);
    }
    return *this;
}

std::vector<BonePose>& AnimationTrack::writable() {
    if (!keys_) {
        keys_ = std::make_shared<std::vector<BonePose>>();
    } else if (keys_->size() != count_) {
        const BonePose key = keys_->front();
        keys_ = std::make_shared<std::vector<BonePose>>(count_, key);
    } else if (keys_.use_count() != 1) {
        keys_ = std::make_shared<std::vector<BonePose>>(*keys_);
    }
    return *keys_;
}

void AnimationTrack::push_back(const BonePose& key) {
    const BonePose value = key; // the argument may refer to this track's data before detach/expansion
    auto& keys = writable();
    keys.push_back(value);
    count_ = keys.size();
}

void AnimationTrack::reserve(std::size_t count) { writable().reserve(count); }

void AnimationTrack::resize(std::size_t count) {
    if (count == 0) {
        clear();
        return;
    }
    writable().resize(count);
    count_ = count;
}

void AnimationTrack::compact() {
    if (!keys_ || keys_->size() <= 1) return;
    const auto& first = keys_->front();
    if (std::all_of(keys_->begin() + 1, keys_->end(), [&](const auto& p) { return same_pose(p, first); })) {
        const BonePose key = first;
        keys_ = std::make_shared<std::vector<BonePose>>(1, key);
        // Keep count_: even identical keys went through blend() when the original track had more than one.
    }
}

std::uint64_t AnimationTrack::hash() const {
    std::uint64_t h = 14695981039346656037ULL;
    h = (h ^ static_cast<std::uint64_t>(stored_keys())) * 1099511628211ULL;
    if (keys_)
        for (const auto& p : *keys_)
            for (auto word : pose_words(p)) h = (h ^ word) * 1099511628211ULL;
    return h;
}

bool AnimationTrack::share_if_equal(const AnimationTrack& other) {
    if (keys_ == other.keys_) return true;
    if (!keys_ || !other.keys_ || keys_->size() != other.keys_->size()) return false;
    if (!std::equal(keys_->begin(), keys_->end(), other.keys_->begin(), same_pose)) return false;
    keys_ = other.keys_; // logical lengths may differ for constant tracks and are deliberately kept per track
    return true;
}

std::size_t AnimationTrack::storage_bytes(StorageSet& seen) const {
    if (!keys_ || !seen.insert(keys_.get()).second) return 0;
    return sizeof(std::vector<BonePose>) + keys_->size() * sizeof(BonePose);
}

void CharacterModel::compact_animations() {
    // Intern only this model's live tracks: no global pool or weak-entry table grows as a client explores zones.
    std::unordered_multimap<std::uint64_t, const AnimationTrack*> pool;
    for (auto& [name, clip] : clips) {
        (void)name;
        for (auto& track : clip.tracks) {
            if (track.empty()) continue;
            track.compact();
            const auto hash = track.hash();
            const auto range = pool.equal_range(hash);
            bool shared = false;
            for (auto it = range.first; it != range.second; ++it)
                if (track.share_if_equal(*it->second)) {
                    shared = true;
                    break;
                }
            if (!shared) pool.emplace(hash, &track);
        }
    }
}

} // namespace hailstone::assets
