#pragma once

// Glue between net::GameConnection's spell state and the spell windows
// (ui/spell_windows.h): builds the view models each frame, draws them and
// passes the player's clicks back to the connection.

#include <cstdint>
#include <memory>
#include <string>

#include "ui/spell_windows.h"

namespace nmsc { class GameDir; }
namespace nmsc::render { class VkContext; }
namespace nmsc::net { class GameConnection; }
namespace nmsc::game { struct SpellData; class EqStrings; }
namespace nmsc::game::mechanics { class Character; }

namespace nmsc::app {

class SpellUi {
public:
    SpellUi(render::VkContext& vk, const GameDir& game);
    ~SpellUi();

    // `character` (the stats port) gives the descriptions CalcAffectChange and SpellDuration.
    void draw(net::GameConnection& conn, const game::mechanics::Character* character = nullptr);
    // Asks the connection to open or close the book on the next draw
    // (GameConnection::open_spell_book / close_spell_book).
    void toggle_book() { toggle_ = !toggle_; }
    bool book_open() const { return book_.open; }

private:
    std::unique_ptr<ui::SpellIconCache> icons_;
    ui::SpellBookView book_;
    bool toggle_ = false;
};

// Who reads a description: the player's level and class and the stats port
// (game/spell_description.h).
struct DescriptionPlayer {
    const game::mechanics::Character* character = nullptr;
    int level = 1;
    std::uint8_t class_id = 0;
};
DescriptionPlayer description_player(const net::GameConnection& conn, const game::mechanics::Character* character);

// The tooltip of a spell: name, level, mana, times, target and the dbstr
// description with its codes filled in as GetSpellDescriptionText does
// (game::spell_description_text).
std::string spell_tooltip(const game::SpellData& spell, const game::EqStrings& strings, int level_needed,
                          const DescriptionPlayer& player = {});
std::string spell_description(const game::SpellData& spell, const game::EqStrings& strings,
                              const DescriptionPlayer& player = {});

} // namespace nmsc::app
