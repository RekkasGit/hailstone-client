#include "game/cursor_drop.h"

namespace hailstone::game {

DropDecision ground_drop_decision(bool no_ground, std::int32_t no_safe_drop) {
    if (no_ground) return DropDecision::Refused;
    if (no_safe_drop == 1) return DropDecision::DropNow;
    if (no_safe_drop == -1) return DropDecision::Disabled;
    return DropDecision::Confirm;
}

std::vector<DropStep> drop_held_item_steps(const DropInput& in) {
    std::vector<DropStep> s;
    if (!in.cursor_item) return s;
    using K = DropStep::Kind;
    if (in.announce) s.push_back({K::Chat, kStrDropped});
    s.push_back({K::RemoveCursorItem, 33});
    if (!in.quiet_net_log && !in.skip_packet) {
        s.push_back({in.connected ? K::Send : K::Log, kOpGroundSpawnDrop});
        if (in.connected && !in.send_ok) s.push_back({K::CloseConnection});
    }
    return s;
}

} // namespace hailstone::game
