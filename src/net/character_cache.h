#pragma once

// GameConnection::with_cached_character's state (net/game_connection.h): the MQ TLOs' shared sheet and Characters.
// A header of its own because the connection's header only declares Character.

#include <cstdint>
#include <memory>
#include <optional>

#include "game/mechanics/character.h"
#include "net/game_connection.h"

namespace hailstone::net {

struct GameConnection::CharacterCache {
    game::mechanics::CharacterSheet sheet;
    std::shared_ptr<const game::SpellTable> spells; // the table the Characters look spells up in, kept alive
    std::optional<game::mechanics::Character> effects, stats;
    std::uint64_t serial = 0; // character_serial_ when built
};

} // namespace hailstone::net
