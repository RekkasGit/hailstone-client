#include "game/eye_control.h"

#include <cctype>

#include "game/world_state.h"

namespace hailstone::game {
namespace {
bool matches(std::string_view name, std::string_view prefix) {
    if (name.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(name[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) return false;
    return true;
}
}

std::string eye_name_for(std::string_view player) { return "Eye_of_" + std::string(player.substr(0, 24)); }

std::uint32_t find_eye_summon(const WorldState& world) {
    const Spawn* player = world.player();
    if (!player) return 0;
    const std::string prefix = eye_name_for(player->name);
    std::uint32_t found = 0;
    for (const auto& [id, spawn] : world.spawns) {
        if (id == world.player_id || spawn.is_corpse || !matches(spawn.name, prefix)) continue;
        // The original takes the first partial-name match in its actor list. This world's hash map has no actor
        // order: refuse an ambiguous summon rather than take control of a different player's same-prefix Eye.
        if (found) return 0;
        found = id;
    }
    return found;
}

bool take_eye_control(WorldState& world, std::uint32_t eye) {
    if (eye == 0 || eye != find_eye_summon(world) || world.eye_id == eye) return false;
    const auto old = world.controlled_id ? world.controlled_id : world.player_id;
    if (auto it = world.spawns.find(old); it != world.spawns.end()) {
        it->second.speed_run = 0.0f;
        it->second.speed_heading = 0.0f;
    }
    world.eye_id = world.controlled_id = world.view_id = eye;
    return true;
}

bool release_eye_control(WorldState& world) {
    if (!world.eye_id) return false;
    if (auto it = world.spawns.find(world.eye_id); it != world.spawns.end()) {
        it->second.speed_run = it->second.speed_heading = 0.0f;
    }
    world.eye_id = world.view_id = 0;
    const Spawn* player = world.player();
    const auto mount = player ? world.spawns.find(player->mount_id) : world.spawns.end();
    world.controlled_id = mount != world.spawns.end() && !mount->second.is_corpse ? mount->first : 0;
    return true;
}
} // namespace hailstone::game
