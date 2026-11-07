#include "game/interact.h"

#include "game/world_state.h"

namespace nmsc::game {

DuelRequestOutcome duel_request(DuelState& s, std::uint32_t challenger, bool ignored) {
    // Handle_OP_RequestDuel @0x4b67c0.
    using K = DuelRequestOutcome::Kind;
    if (ignored) return {K::Refuse, 0};
    if (s.active) {
        if (s.challenged == challenger) return {K::Ignore, 0};
        return {K::Refuse, 1};
    }
    if (s.challenged == challenger) {
        // Our challenge came back: the target accepted (eqstr 1424).
        s.active = true;
        s.challenger = challenger;
        return {K::Accepted, 0};
    }
    if (s.challenged != 0) return {K::Refuse, 2};
    if (s.challenger == 0) {
        s.challenger = challenger;
        return {K::Challenged, 0};
    }
    if (s.challenger == challenger) return {K::Ignore, 0};
    return {K::Refuse, 2};
}

DuelCommandOutcome duel_command(DuelState& s, std::uint32_t target, bool target_is_pc, bool target_is_me) {
    // Cmd_duel @0x4f6930.
    using K = DuelCommandOutcome::Kind;
    if (target == 0 || !target_is_pc) return {K::Message, 13248};
    if (target_is_me) return {K::Message, 13249};
    if (s.active) return {K::Message, s.challenged == target ? 13250u : 13251u};
    if (s.challenger == target) {
        s.active = true;
        s.challenged = target;
        return {K::Accept, 13252};
    }
    if (s.challenger != 0) return {K::Message, 13253};
    s.challenged = target;
    return {K::Challenge, 13254};
}

bool opens_player_menu(const Spawn& sp, bool self) {
    return !self && !sp.is_npc && !sp.is_corpse && !sp.trader && !sp.buyer;
}

} // namespace nmsc::game
