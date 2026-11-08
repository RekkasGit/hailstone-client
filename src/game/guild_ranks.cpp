#include "game/guild_ranks.h"

namespace hailstone::game {

std::int32_t guild_rank_compare(std::int32_t a, std::int32_t b) {
    auto ua = static_cast<std::uint32_t>(a), ub = static_cast<std::uint32_t>(b);
    return static_cast<std::int32_t>(a != 0 && b != 0 ? ub - ua : ua - ub);
}

bool guild_rank_above(std::int32_t rank, std::int32_t other) { return guild_rank_compare(rank, other) > 0; }

bool guild_rank_at_least(std::int32_t rank, std::int32_t other) { return guild_rank_compare(rank, other) >= 0; }

bool guild_member_rank_at_least(const std::int32_t* a_rank, const std::int32_t* b_rank) {
    if (!a_rank || !b_rank) return false;
    return guild_rank_compare(*a_rank, *b_rank) >= 0;
}

std::int32_t guild_permission_word_index(std::int32_t rank, std::int32_t permission) {
    std::uint32_t index = static_cast<std::uint32_t>(permission / 32) + static_cast<std::uint32_t>(rank) * 2u + 1u;
    return static_cast<std::int32_t>(index);
}

bool guild_permission_bit(std::uint32_t word, std::int32_t permission) {
    auto shift = static_cast<std::uint32_t>(permission % 32) & 31u;
    return (word & (1u << shift)) != 0;
}

bool GuildPermissions::has(std::int32_t rank, std::int32_t permission) const {
    std::int32_t i = guild_permission_word_index(rank, permission);
    if (i < 0 || static_cast<std::size_t>(i) >= words.size()) return false;
    return guild_permission_bit(words[static_cast<std::size_t>(i)], permission);
}

void GuildPermissions::set(std::int32_t rank, std::int32_t permission, bool value) {
    std::int32_t i = guild_permission_word_index(rank, permission);
    if (i < 0 || static_cast<std::size_t>(i) >= words.size()) return; // the original writes outside its table
    std::uint32_t bit = 1u << (static_cast<std::uint32_t>(permission % 32) & 31u);
    if (value) words[static_cast<std::size_t>(i)] |= bit;
    else words[static_cast<std::size_t>(i)] &= ~bit;
}

bool local_pc_has_guild_permission(bool have_character, bool is_gm, std::int32_t rank, std::int32_t permission,
                                   const GuildPermissions& table) {
    if (!have_character) return false;
    if (is_gm) return true;
    return table.has(rank, permission);
}

} // namespace hailstone::game
