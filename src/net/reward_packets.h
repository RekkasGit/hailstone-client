#pragma once

// The stock RoF2 Select Reward managers (PendingRewardMgr / RewardSelectionMgr).
// The same payloads use independent claimable (0x6411) and preview (0x6471) lanes.
// Wire layouts checked against EQEmu common/achievements.cpp and the native senders
// 0x463b50, 0x463cd0, 0x463d80 and 0x464cc0. No provider-specific authorization lives here.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "net/item_packets.h"
#include "net/opcodes.h"
#include "net/packet.h"

namespace hailstone::net {

inline constexpr std::uint16_t kOpPendingRewards = 0x6411, kOpRewardPreview = 0x6471;
enum class RewardLane : std::uint8_t { Pending, Preview };
inline std::uint16_t reward_opcode(RewardLane lane) {
    return to_wire(lane == RewardLane::Pending ? Op::AchievementReward : Op::RewardSelection);
}

enum class RewardEntryType : std::uint32_t {
    Text = 0, Money = 1, Item = 2, Experience = 3, AAAbility = 4, AAPoints = 5, GenericPoints = 11
};
struct RewardItem {
    std::uint32_t id = 0, quantity = 0;
    bool lore = false;
    std::uint32_t lore_group = 0;
    std::string name;
};
struct RewardEntry {
    RewardEntryType type = RewardEntryType::Text;
    std::uint32_t entry_id = 0, string_id = 0;
    std::string description;
    // Money: PP/GP/SP/CP; AAAbility: rank id/cumulative rank; AAPoints: amount;
    // GenericPoints: amount at [2], currency id at [3]. Experience has no numeric suffix.
    std::array<std::uint32_t, 4> values{};
    std::uint8_t flag = 0;
    std::vector<RewardItem> items;
};
struct RewardOption {
    std::uint32_t wire_id = 0;
    bool common = false;
    std::uint8_t flags = 0;
    std::string label;
    std::vector<RewardEntry> entries;
};
struct RewardSet {
    std::uint32_t pending_id = 0, set_id = 0;
    std::string title;
    float multiplier = 1.0f;
    std::vector<RewardOption> options;
};
struct RewardClaimReply {
    std::uint32_t pending_id = 0, set_id = 0, wire_option_id = 0;
    bool success = false;
};
struct RewardInspection {
    std::uint32_t item_id = 0;
    Item item;
};
struct RewardPacket {
    // Pending/List upserts by the first wire ID (pending_id); Preview/List and Bulk replace.
    // A false Pending/List is a no-op. Claim and Inspection carry only their own field.
    enum class Kind { Collection, Claim, Inspection } kind = Kind::Collection;
    RewardLane lane = RewardLane::Pending;
    bool replace_collection = true;
    std::vector<RewardSet> sets;
    RewardClaimReply claim;
    std::optional<RewardInspection> inspection;
};

// Host resource bounds, not original-client capacities. All counts are also checked against
// the bytes remaining before allocation; limits apply across the whole collection.
inline constexpr std::size_t kMaxRewardPacketBytes = 8u << 20;
inline constexpr std::size_t kMaxRewardTabs = 256, kMaxRewardOptions = 4096;
inline constexpr std::size_t kMaxRewardEntries = 16384, kMaxRewardItems = 16384;
std::optional<RewardPacket> decode_reward_packet(const AppPacket& p);

AppPacket encode_pending_rewards_request(); // action 6, native 0x463cd0
AppPacket encode_task_reward_preview(std::uint32_t task_id); // action 4, preview lane only
AppPacket encode_achievement_reward_view(std::uint32_t definition_index); // action 5, not achievement ID
AppPacket encode_reward_inspect(RewardLane lane, std::uint32_t set_id, std::uint32_t wire_option_id,
                               std::uint32_t entry_id, std::uint32_t item_id);
AppPacket encode_reward_claim(std::uint32_t pending_id, std::uint32_t set_id, std::uint32_t wire_option_id);

} // namespace hailstone::net
