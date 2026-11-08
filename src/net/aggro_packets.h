#pragma once

// The aggro meter in EQEmu's layouts (zone/client.cpp Client::ProcessAggroMeter, zone/aggromanager.h; RoF2 and
// later only, behind the server's Character:EnableAggroMeter rule). The original's CAggroMeterWnd is not decoded.

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "net/packet.h"

namespace hailstone::net {

// EQEmu's AggroMeter::AggroTypes: 0 the player, 1 the secondary (the next on the hate list when we have aggro, else
// the target's target), 2..6 the group (or raid group) in member order, 7..26 the extended targets.
inline constexpr int kAggroPlayer = 0, kAggroSecondary = 1, kAggroGroup1 = 2, kAggroXTarget1 = 7, kAggroTypes = 27;

// OP_AggroMeterTargetInfo (Z→C, 8 bytes): u32 the locked target, u32 the target the meter follows.
struct AggroTargetInfo {
    std::uint32_t lock_id = 0, target_id = 0;
};
std::optional<AggroTargetInfo> decode_aggro_target_info(const AppPacket& p);
AppPacket encode(const AggroTargetInfo& v); // server direction, for tests

// OP_AggroMeterUpdate (Z→C): u8 secondary changed, then (when set) u32 the secondary's id; u8 count, count × {u8
// type, u16 percent} (only the entries that changed).
struct AggroUpdate {
    std::optional<std::uint32_t> secondary_id;
    std::vector<std::pair<std::uint8_t, std::uint16_t>> entries;
};
std::optional<AggroUpdate> decode_aggro_update(const AppPacket& p);
AppPacket encode(const AggroUpdate& v); // server direction, for tests

// OP_AggroMeterLockTarget (C→Z, 4 bytes): the spawn to lock the meter on (0 unlocks).
AppPacket encode_aggro_lock(std::uint32_t spawn_id);

} // namespace hailstone::net
