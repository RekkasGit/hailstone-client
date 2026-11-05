#pragma once

// Developer spell windows drawn with Dear ImGui until the stock UI engine
// (M5): the spell gem bar (CastSpellWnd), the casting bar, the spell book,
// the buff and song windows, and the target's buffs. Like ui/dev_screens.h
// they draw from a view model and return what the player asked for; none of
// them does networking.
//
// Spell icons come from the stock UI's A_SpellIcons animation
// (uifiles/default/EQUI_Animations.xml): icon n is cell n % 36 of
// spells0<n / 36 + 1>.tga, 40 x 40 pixel cells in a horizontal grid (across
// each row first), 6 per row of a 256 x 256 sheet.

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace nmsc { class GameDir; }
namespace nmsc::render { class VkContext; }

namespace nmsc::ui {

struct SpellIconCell {
    int sheet = 0;    // 1-based: spells0<sheet>.tga
    int x = 0, y = 0; // top-left pixel of the 40 x 40 cell
};
std::optional<SpellIconCell> spell_icon_cell(std::int32_t icon);

// Lazily loads the spell icon sheets as ImGui textures.
class SpellIconCache {
public:
    SpellIconCache(render::VkContext& vk, const GameDir& game);
    ~SpellIconCache();
    SpellIconCache(const SpellIconCache&) = delete;
    SpellIconCache& operator=(const SpellIconCache&) = delete;

    // Draws the icon (a placeholder when it is missing) into a draw list at a screen rectangle.
    bool draw_at(void* draw_list, std::int32_t icon, float x, float y, float size, std::uint32_t tint = 0xffffffff);

private:
    struct Sheet;
    const Sheet* sheet(int number);

    render::VkContext& vk_;
    const GameDir& game_;
    void* sampler_ = nullptr; // VkSampler
    std::map<int, std::unique_ptr<Sheet>> sheets_;
};

struct SpellGemView {
    std::uint32_t spell = 0xffffffff; // empty gem
    std::string name;
    std::int32_t icon = -1;
    float refresh = 0.0f;   // 0 ready .. 1 just cast (the gem's tint)
    bool usable = true;     // gems 9..12 may be locked
    bool casting = false;   // the gem being cast
    float memorize = -1.0f; // 0..1 while this gem is being memorized
    std::string tooltip;
};

struct SpellBarView {
    std::vector<SpellGemView> gems; // 12
    bool show_book_button = true;
};

struct CastBarView {
    bool visible = false;
    std::string spell;
    float fraction = 0.0f; // done, 0..1
    float seconds_left = 0.0f;
    std::string memorizing; // "Memorizing X" when a memorization runs
    float memorize_fraction = -1.0f;
};

struct SpellBookEntry {
    int slot = 0;
    std::uint32_t spell = 0xffffffff;
    std::string name;
    std::int32_t icon = -1;
    int level = 0;
    std::string tooltip;
    bool memorized = false;
};

struct SpellBookView {
    bool open = false;
    std::vector<SpellBookEntry> entries; // every non-empty slot
    int page = 0;                        // 16 slots per page (two 8-slot pages per spread in the original)
    int target_gem = -1;                 // the gem a click memorizes into (-1: the first empty one)
    int usable_gems = 8;
    std::string status;                  // "Memorizing ...", level-up hints
    bool cursor_scroll = false;          // a spell scroll is on the cursor: a click scribes it
};

struct BuffView {
    int slot = 0;
    std::uint32_t spell = 0xffffffff;
    std::string name;
    std::int32_t icon = -1;
    std::string time_left; // "4:59", "permanent"
    bool beneficial = true;
    std::string tooltip;
};

struct BuffWindowView {
    std::vector<BuffView> buffs; // long buffs
    std::vector<BuffView> songs; // short buff window
    std::string target_name;     // empty: no target buffs
    std::vector<BuffView> target;
};

struct SpellActions {
    std::optional<int> cast_gem;
    std::optional<int> forget_gem;
    std::optional<std::pair<int, int>> memorize; // book slot, gem
    std::optional<int> scribe;                   // book slot for the scroll on the cursor
    std::optional<int> remove_buff;
    bool stop_cast = false;
    bool toggle_book = false;
};

void spell_bar_window(const SpellBarView& view, SpellActions& actions, SpellIconCache* icons);
void cast_bar_window(const CastBarView& view);
void spell_book_window(SpellBookView& view, SpellActions& actions, SpellIconCache* icons);
void buff_windows(const BuffWindowView& view, SpellActions& actions, SpellIconCache* icons);

} // namespace nmsc::ui
