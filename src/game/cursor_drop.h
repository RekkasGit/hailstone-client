#pragma once

// Dropping the cursor's item on the ground: rof2decomp batch 253
// (re/src/game/ui/cursor_drop_eh.cpp; LMouseUp answered from its
// disassembly), with CEverQuest::DropHeldItemOnGround @0x526290 checked
// against CEverQuest_DropHeldItemOnGround.jsonl.
//  - LMouseUp: a NoGround item says 7697; then NoSafeDrop (eqclient.ini
//    [Options], /fastdrop) 1 drops at once (announced), -1 says 12320, any
//    other value asks 9057 ("ARE YOU SURE...") and a yes drops (not
//    announced).
//  - DropHeldItemOnGround(announce): nothing without a cursor item; announced
//    says 9058 (colour 15); the item leaves the cursor; OP_GroundSpawn 0x6fca
//    goes out with an empty payload unless net logging is quiet or 0xddf790 is
//    set (without a connection it is only logged; a failed send closes the
//    connection); PcClient +0x2dd4 = 1.

#include <cstdint>
#include <vector>

namespace hailstone::game {

inline constexpr std::uint32_t kStrNoGround = 7697, kStrDropDisabled = 12320, kStrDropConfirm = 9057,
                               kStrDropped = 9058;
inline constexpr std::uint16_t kOpGroundSpawnDrop = 0x6fca;

enum class DropDecision : std::uint8_t {
    Refused,  // NoGround: 7697
    Disabled, // NoSafeDrop -1: 12320
    DropNow,  // NoSafeDrop 1: DropHeldItemOnGround(1)
    Confirm,  // ask 9057; yes: DropHeldItemOnGround(0)
};
DropDecision ground_drop_decision(bool no_ground, std::int32_t no_safe_drop);

struct DropStep {
    enum class Kind : std::uint8_t { Chat, RemoveCursorItem, Send, Log, CloseConnection };
    Kind kind{};
    std::uint32_t value = 0; // the eqstr id, the slot or the opcode
};
struct DropInput {
    bool announce = false;
    bool cursor_item = true;
    bool quiet_net_log = false; // 0xddf654
    bool skip_packet = false;   // 0xddf790
    bool connected = true;      // __gWorld
    bool send_ok = true;        // UdpConnection::Send's answer (false: NetMgr_CloseConnection)
};
// DropHeldItemOnGround's steps in order; empty without a cursor item.
std::vector<DropStep> drop_held_item_steps(const DropInput& in);

} // namespace hailstone::game
