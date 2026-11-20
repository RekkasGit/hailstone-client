// GameConnection: OP_GMLastName and OP_LevelAppearance (net/rename_packets.h).

#include <algorithm>
#include <cctype>

#include "core/ascii_case.h"
#include "core/log.h"
#include "net/game_connection.h"

namespace hailstone::net {

namespace {

// The latest level-up effects kept for a later renderer (see apply_level_appearance).
constexpr std::size_t kLevelAppearanceKept = 16;

bool same_name(std::string_view a, std::string_view b) {
    // GetSpawnByName compares with a case-insensitive string compare (FUN_00809250 -> _stricmp).
    return std::ranges::equal(a, b, [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

} // namespace

std::uint32_t GameConnection::find_spawn_by_name(std::string_view name) const {
    if (name.empty()) return 0; // GetSpawnByName answers none for an empty name
    for (const auto& [id, spawn] : world_.spawns)
        if (same_name(spawn.name, name)) return id;
    return 0;
}

bool GameConnection::handle_spawn_rename(const AppPacket& p) {
    switch (from_wire(p.opcode)) {
    case Op::GMLastName:
        if (auto r = decode_gm_last_name(p)) apply_gm_last_name(*r);
        return true;
    case Op::LevelAppearance:
        if (auto r = decode_level_appearance(p)) apply_level_appearance(*r);
        return true;
    default:
        return false;
    }
}

void GameConnection::apply_gm_last_name(const GMLastName& r) {
    // Handle_OP_GMLastName @0x4bbcd0: the spawn is looked up by name; no spawn or apply (@196) 0 ignores the packet.
    // The last name is copied to the spawn (+0x38), the profile's own too (0x4d5470) for the player, then the
    // lists are rebuilt (0x58f050).
    if (!r.apply) return;
    const std::uint32_t id = find_spawn_by_name(r.name);
    if (id == 0) return;
    world_.spawns.at(id).last_name = r.last_name;
    if (id == world_.player_id && profile_) profile_->last_name = r.last_name;
    ++world_.spawn_set_serial;
}

void GameConnection::apply_level_appearance(const LevelAppearance& r) {
    // The case at 0x4d0053 / 0x58fed0: the spawn is found by id; each group with an effect id above 0 and an attach
    // point in the spawn model's table hangs the effect there. How an effect id maps to a file in the game folder is
    // not derived (docs/unknowns.md), so the packet is decoded, kept (the latest few) and logged, and nothing is drawn.
    if (!world_.spawns.contains(r.spawn_id)) return;
    for (const auto& g : r.groups) {
        if (!level_appearance_group_used(g)) continue;
        log::info("level appearance: spawn {} effect {:#x} slot {} flag {}", r.spawn_id, g.effect_id, g.slot, g.flag);
    }
    level_appearances_.push_back(r);
    while (level_appearances_.size() > kLevelAppearanceKept) level_appearances_.pop_front();
}

} // namespace hailstone::net
