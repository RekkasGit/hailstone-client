#include "game/server_ext.h"

#include "game/spells.h"

namespace nmsc::game {

std::int32_t ServerExtension::combat_ability_timer(const net::GameConnection&, const SpellData& s) const {
    return s.reuse_timer_index;
}

bool ServerExtension::tracking_allowed(const net::GameConnection& c, std::uint8_t class_id, std::uint32_t class_mask,
                                       std::uint32_t tracking_skill) const {
    const SkillRules* rules = skill_rules(c);
    if (rules && rules->tracking_allowed) return rules->tracking_allowed(rules->ctx, class_id, class_mask, tracking_skill);
    return game::tracking_allowed()(class_id, class_mask, tracking_skill);
}

ServerExtension& stock_server_extension() {
    static ServerExtension stock;
    return stock;
}

} // namespace nmsc::game
