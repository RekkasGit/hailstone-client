// Movement packets the client sends on its own (net/movement_packets.h).

#include "game/mechanics/character.h"
#include "net/game_connection.h"
#include "net/movement_packets.h"

namespace nmsc::net {

void GameConnection::report_jump(std::int32_t endurance_cost) {
    if (state_ != ConnectionState::InZone) return;
    send_zone(encode_jump());
    std::int64_t left = static_cast<std::int64_t>(pstatus_.endurance) - endurance_cost;
    pstatus_.endurance = static_cast<std::uint32_t>(left < 0 ? 0 : left);
}

void GameConnection::report_env_damage(std::uint32_t damage, std::uint8_t type) {
    if (state_ != ConnectionState::InZone || world_.player_id == 0 || damage == 0) return;
    send_zone(encode(EnvDamage{world_.player_id, damage, type}));
    pstatus_.hp -= damage;
    // ApplyDamage's hit report (rof2decomp re/src/game/character/apply_damage.cpp; its callers pass report 1):
    // unless the HP fell to the floor (-10 - SPA 0x148: the knock-out path), OP_Damage (0x6f15) with the hit {the
    // player, no attacker, the type, spell -1, the damage}, which EQEmu passes on to the clients close by (back to
    // the sender too, but for a fall), and CEverQuest::ReportSuccessfulHit(hit, 1) on it here: the line, the
    // sounds and the animation.
    std::int32_t below = 0;
    with_character([&](game::mechanics::Character& ch) { below = ch.total_effect(0x148, true, 0, true, true); });
    if (pstatus_.hp <= -10 - static_cast<std::int64_t>(below)) return;
    CombatDamage hit;
    hit.target = static_cast<std::uint16_t>(world_.player_id);
    hit.type = type;
    hit.spell_id = 0xffffffffu;
    hit.damage = static_cast<std::int32_t>(damage);
    send_zone(encode(hit));
    report_successful_hit(hit, true);
}

void GameConnection::report_safe_fall() {
    if (state_ == ConnectionState::InZone) send_zone(encode_safe_fall_success());
}

void GameConnection::set_running(bool running) {
    if (running == running_) return;
    running_ = running;
    if (state_ == ConnectionState::InZone) send_zone(encode_set_run_mode(running));
}

} // namespace nmsc::net
