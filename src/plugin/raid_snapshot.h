#pragma once

// The raid roster in the shape get_raid_data_bulk and the hailstone Lua module give it. One pass over the roster,
// resolving each member's spawn once, so a plugin or script does not run a TLO expression per member.
//
// The data is the client's own: game::RaidState for the roster and roles, game::GroupState for the mana and
// endurance percentages it receives for group members, and the world's spawns for HP, position and the pet. A
// member outside this zone has no spawn: spawn_id 0 and the percentages -1.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/ascii_case.h"
#include "game/social.h"
#include "game/world_state.h"
#include "net/game_connection.h"
#include "hailstone_plugin.h"

namespace hailstone::plugin {

struct RaidMemberSnapshot {
    std::string name;
    std::uint32_t spawn_id = 0;
    std::int32_t group = -1;
    std::uint32_t flags = 0;
    std::int32_t level = 0;
    std::int32_t class_id = 0;
    std::int32_t pct_hp = -1;
    std::int32_t pct_mana = -1;
    std::int32_t pct_endurance = -1;
    std::uint32_t pet_spawn_id = 0;
    std::int32_t pet_pct_hp = -1;
    float x = 0, y = 0, z = 0, distance = 0;
};

struct RaidSnapshot {
    bool active = false;
    bool locked = false;
    std::uint32_t loot_type = 1;
    std::optional<std::size_t> leader;
    std::optional<std::size_t> target;
    std::vector<RaidMemberSnapshot> members;
};

namespace detail {

// A player or a virtual player's spawn by its roster name: the raw name first (a player's), then the clean name
// (a mercenary's). Corpses are left out.
inline const game::Spawn* raid_spawn(const net::GameConnection& c, std::string_view name) {
    for (const auto& entry : c.world().spawns) {
        const game::Spawn& s = entry.second;
        if (s.is_corpse) continue;
        if (!s.is_npc && iequals(s.name, name)) return &s;
    }
    for (const auto& entry : c.world().spawns) {
        const game::Spawn& s = entry.second;
        if (s.is_corpse) continue;
        if (iequals(game::display_name(s.name), name)) return &s;
    }
    return nullptr;
}

inline bool listed_looter(const game::RaidState& r, std::string_view name) {
    for (const auto& n : r.looters)
        if (!n.empty() && iequals(n, name)) return true;
    return false;
}

} // namespace detail

inline RaidSnapshot build_raid_snapshot(const net::GameConnection& c) {
    RaidSnapshot out;
    const game::RaidState& raid = c.raid();
    out.active = raid.active;
    out.locked = raid.locked;
    out.loot_type = raid.loot_type;
    const game::Spawn* self = c.world().player();
    out.members.reserve(raid.members.size());
    for (std::size_t i = 0; i < raid.members.size(); ++i) {
        const game::RaidMember& m = raid.members[i];
        RaidMemberSnapshot s;
        s.name = m.name;
        const game::Spawn* spawn = detail::raid_spawn(c, m.name);
        const bool is_me = iequals(m.name, c.character_name());
        if (spawn) {
            s.spawn_id = spawn->id;
            if (is_me) {
                const auto hp = c.own_hp();
                s.pct_hp = hp.max > 0 ? static_cast<std::int32_t>(std::max<std::int64_t>(0, hp.cur * 100 / hp.max)) : 0;
            } else {
                s.pct_hp = static_cast<std::int32_t>(spawn->hp_percent);
            }
            s.x = spawn->position.x;
            s.y = spawn->position.y;
            s.z = spawn->position.z;
            if (self) {
                const float dx = spawn->position.x - self->position.x;
                const float dy = spawn->position.y - self->position.y;
                const float dz = spawn->position.z - self->position.z;
                s.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            }
        }
        s.group = m.group == 0xffffffffu ? -1 : static_cast<std::int32_t>(m.group);
        s.level = m.level ? static_cast<std::int32_t>(m.level) : (spawn ? static_cast<std::int32_t>(spawn->level) : 0);
        s.class_id = m.class_ ? static_cast<std::int32_t>(m.class_) : (spawn ? static_cast<std::int32_t>(spawn->class_id) : 0);

        // Mana and endurance: the player's own, or the group update the client keeps for a group member. A raid
        // member outside the player's group has neither.
        if (is_me) {
            const auto& ps = c.player_status();
            s.pct_mana = ps.max_mana ? static_cast<std::int32_t>(ps.mana) * 100 / static_cast<std::int32_t>(ps.max_mana) : -1;
            s.pct_endurance = ps.max_endurance
                                  ? static_cast<std::int32_t>(ps.endurance) * 100 / static_cast<std::int32_t>(ps.max_endurance)
                                  : -1;
        } else if (spawn) {
            const auto mana = c.group().mana_percent.find(spawn->id);
            if (mana != c.group().mana_percent.end()) s.pct_mana = mana->second;
            const auto endurance = c.group().endurance_percent.find(spawn->id);
            if (endurance != c.group().endurance_percent.end()) s.pct_endurance = endurance->second;
        }

        // The pet: the pet window's for the player, else the first spawn it owns.
        if (spawn) {
            if (is_me && c.pets().focused) {
                s.pet_spawn_id = c.pets().focused;
            } else {
                for (const auto& entry : c.world().spawns) {
                    const game::Spawn& o = entry.second;
                    if (o.pet_owner_id == spawn->id && !o.is_corpse) {
                        s.pet_spawn_id = o.id;
                        break;
                    }
                }
            }
            if (s.pet_spawn_id) {
                const auto pet = c.world().spawns.find(s.pet_spawn_id);
                if (pet != c.world().spawns.end()) s.pet_pct_hp = static_cast<std::int32_t>(pet->second.hp_percent);
            }
        }

        const bool leader = iequals(m.name, raid.leader);
        bool main_assist = m.main_assist;
        if (!main_assist)
            for (const auto& name : c.leadership().raid_assist_names)
                if (!name.empty() && iequals(name, m.name)) {
                    main_assist = true;
                    break;
                }
        bool looter = leader || (raid.loot_type == 2 && m.group_leader);
        if (!looter && raid.loot_type == 3) looter = detail::listed_looter(raid, m.name);
        if (raid.loot_type == 4) looter = true; // Entire Raid: everyone may loot
        if (leader) s.flags |= HS_BULK_RAID_LEADER;
        if (m.group_leader) s.flags |= HS_BULK_RAID_GROUP_LEADER;
        if (main_assist) s.flags |= HS_BULK_RAID_MAIN_ASSIST;
        if (m.marker) s.flags |= HS_BULK_RAID_MARKER;
        if (m.waypointer) s.flags |= HS_BULK_RAID_WAYPOINTER;
        if (looter) s.flags |= HS_BULK_RAID_LOOTER;
        if (spawn) s.flags |= HS_BULK_RAID_PRESENT;
        if (is_me) s.flags |= HS_BULK_RAID_ME;
        out.members.push_back(std::move(s));
    }
    for (std::size_t i = 0; i < raid.members.size(); ++i) {
        if (!out.leader && iequals(raid.members[i].name, raid.leader)) out.leader = i;
        if (!out.target && iequals(raid.members[i].name, raid.target)) out.target = i;
    }
    return out;
}

} // namespace hailstone::plugin
