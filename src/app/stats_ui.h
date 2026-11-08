#pragma once

// The character / stats window (developer UI): the local player's stats as
// the original client computes them (game/mechanics: ProcessAllStats, Max_HP,
// regen, skills ...), next to the server's maxima, the way the original's
// inventory window shows them. Logs a warning when the computed maximum HP,
// mana or endurance differs from what the server sends (information only when
// a server's client patch answers them with the server's values, the NMS
// patch's detours: server_ext().character_vitals).

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace nmsc { class GameDir; }
namespace nmsc::net { class GameConnection; }
namespace nmsc::game::mechanics {
struct GameTables;
struct CharacterSheet;
class Character;
}

namespace nmsc::app {

// Tells the connection what its player's own HP leaves out (net::GameConnection::own_hp): the character's items' HP
// (DerivedStats::hp_bonus) for each of Cur_HP and Max_HP a server extension does not give whole; nothing without a
// character.
void share_own_hp_items(net::GameConnection& conn, const game::mechanics::Character* ch);

class StatsUi {
public:
    explicit StatsUi(const GameDir& game);
    ~StatsUi();

    void draw(net::GameConnection& conn);
    void toggle() { open_ = !open_; }
    void set_open(bool open) { open_ = open; }
    bool is_open() const { return open_; }
    // The player as the original computes it, rebuilt by draw() each frame
    // (null before the first in-zone frame). Movement reads it for STR, STA,
    // skills and TotalEffect.
    const game::mechanics::Character* character() const { return character_.get(); }
    // Drops that player. It reads its connection's spell table (spell_lookup) and server extension
    // (CharacterSheet::skill_rules), which go with the connection: the app calls this before it drops or replaces
    // one, and draw() does when not in a zone (camping counts as in one).
    void forget();

private:
    std::unique_ptr<game::mechanics::GameTables> tables_;
    // Character keeps a reference to its sheet, so both live here.
    std::unique_ptr<game::mechanics::CharacterSheet> sheet_;
    std::unique_ptr<game::mechanics::Character> character_;
    std::unique_ptr<game::mechanics::CharacterSheet> class_sheet_; // the per-class comparison's copy, reused
    bool open_ = false;
    std::int64_t warned_hp_[2] = {-1, -1};   // computed, server: the last mismatch logged
    std::int64_t warned_mana_[2] = {-1, -1};
    std::int64_t warned_end_[2] = {-1, -1};
    std::chrono::steady_clock::time_point next_check_{};
};

} // namespace nmsc::app
