// GameConnection: the packets the skills and abilities code (app/actions_ui.cpp) reads.
#include "net/game_connection.h"

#include "game/merchant.h"
#include "net/ability_packets.h"
#include "net/notice_packets.h"

namespace hailstone::net {

void GameConnection::handle_ability(Op op, const AppPacket& p) {
    switch (op) {
    case Op::SkillUpdate:
        // Handle_OP_SkillUpdate @0x4be7c0 (net/game_connection_progress.cpp).
        handle_skill_update(p);
        break;
    case Op::Track:
        // HandleWorldMessage @0x4c623c: CTrackingWnd::SetTrackingList (game/tracking.h).
        handle_track_reply(p);
        break;
    case Op::Begging:
    case Op::PickPocket:
        server_coin(op, p);
        break;
    case Op::SpawnAppearance:
    case Op::CancelSneakHide:
    case Op::Taunt:
    case Op::FeignDeath:
    case Op::Bind_Wound:
        break;
    default:
        return;
    }
    // Nobody may be reading (tools): keep only the latest few.
    if (ability_packets_.size() >= 64) ability_packets_.pop_front();
    ability_packets_.push_back(p);
}

void GameConnection::server_coin(Op op, const AppPacket& p) {
    // Coin the server adds without a coin update (AddMoneyToPP, no client update): begging (3 silver, 4 copper:
    // the amount in that coin) and pick pockets (1..4 platinum .. copper; a stolen item comes as an item packet).
    auto f = op == Op::Begging ? decode_begging(p) : decode_pick_pocket(p);
    if (!f || f->amount == 0) return;
    if (op == Op::Begging) {
        if (f->kind == 4) game::server_add_copper(coins_, f->amount);
        else if (f->kind == 3) game::server_add_copper(coins_, static_cast<std::uint64_t>(f->amount) * 10);
    } else if (f->kind == 1) {
        coins_.platinum += f->amount;
    } else if (f->kind == 2) {
        coins_.gold += f->amount;
    } else if (f->kind == 3) {
        coins_.silver += f->amount;
    } else if (f->kind == 4) {
        coins_.copper += f->amount;
    }
}

std::optional<AppPacket> GameConnection::poll_ability_packet() {
    if (ability_packets_.empty()) return std::nullopt;
    AppPacket p = std::move(ability_packets_.front());
    ability_packets_.pop_front();
    return p;
}

} // namespace hailstone::net
