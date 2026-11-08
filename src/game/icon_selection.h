#pragma once

// The icon selection window (CIconSelectionWnd, screen "IconSelectionWnd", EQUI_IconSelectionWnd.xml) and the icon
// caches it picks from (InitIconCaches @0x665520, __GetAnimationCache @0x665430):
//   type 0  A_DragItem           icons 500 .. 500 + cells   (item icons; 4999 without the animation)
//   type 1  A_SpellGems          icons 0 .. cells           (gem icons; 299 without it)
//   type 2  A_MenuIcons          icons 0 .. cells           (0x23 without it)
//   type 3  A_SpeakingIndicator  icons 0 .. cells           (2 without it; not in the type combo box)
// where cells = the first frame's columns x rows (its size over CellWidth / CellHeight) x the frame count
// (FUN_0087a820). IconCache::GetIcon(n) is cell n - base (500 for items) of the grid animation. The range's upper
// end is exclusive in the window's list and range label, inclusive in SetSelectedIcon's clamp (the original's).
//
// A hotbutton's Assign Icon (CHotButton's menu item 0x1b5a, FUN_0068f330 @0x68f330) opens it on the button's
// {icon type, icon} at the button's centre (kept on the screen): owner = the button, page offset 0, SetIconType
// (a type above 0x7fffffff, the button's -1, is 1: Spell), the type combo set to the type, shown. The window:
//   ISW_Type_Combobox     Item / Spell / Menu: type = choice, offset 0, SetIconType(choice, -1)
//   ISW_PerPage_Combobox  50 / 100 / 500 / 1000 / All (-1): per page, offset 0, SetIconType(type, -1); 50 at first
//   ISW_PageLeft_Button / ISW_PageRight_Button  (per page >= 1) offset -/+ per page, SetIconType(type, selected)
//   ISW_PageIndex_Label   offset / per page + 1 (1 for All); ISW_PageRange_Label "Icons: %d - %d"
//   ISW_Icons             the page's icons (ISW_IconTemplate_Button copies); a click selects that icon
//   ISW_Slot_EditBox      the selected icon's number; Enter or a change selects the number typed
//   ISW_SelectedItem_Button  the selected icon
//   ISW_Accept_Button     hides, and the owner gets notification 0x28 with {type, selected} (sub_0068e710): the
//                         hotbutton's icon becomes that, saved (FUN_00550040)
// CIconSelectionWnd_SetIconType @0x68eee0, CIconSelectionWnd_SetSelectedIcon @0x68e7e0,
// CIconSelectionWnd_UpdateRangeLabel @0x68e8d0, FUN_0068e790 (the page's first and last icon).

#include <cstdint>
#include <optional>
#include <string>

namespace hailstone::game {

inline constexpr int kIconTypes = 4;
// The grid animation of an icon type ("A_DragItem" ...), nullptr outside 0..3.
const char* icon_type_animation(int type);

struct IconCacheRange {
    std::int32_t base = 0;     // IconCache +0x14: icon n is cell n - base
    std::int32_t min = 0;      // +0x18
    std::int32_t max = 0;      // +0x1c (exclusive in the list)
};
// InitIconCaches: the range of type 0..3 with the animation's cell count (nullopt: no such animation).
IconCacheRange icon_cache_range(int type, std::optional<std::int32_t> cells);
// FUN_0087a820: a grid animation's cells (first frame's columns x rows x frames).
std::int32_t grid_cells(int frame_w, int frame_h, int cell_w, int cell_h, int frames);

class IconSelection {
public:
    // The caches' ranges (by type; a type without a range has no cache: SetIconType fails).
    std::optional<IconCacheRange> ranges[kIconTypes];

    // FUN_0068f330 without the placing: false (nothing shown) when the type has no cache.
    bool open(std::int32_t type, std::int32_t selected);
    // CIconSelectionWnd_SetIconType({type, selected}): false when the type has no cache.
    bool set_type(std::int32_t type, std::int32_t selected);
    void set_selected(std::int32_t icon); // CIconSelectionWnd_SetSelectedIcon: clamped to [min, max]
    void choose_type(int choice);         // ISW_Type_Combobox
    void choose_per_page(int choice);     // ISW_PerPage_Combobox (4: All)
    void page_left();
    void page_right();

    std::int32_t type() const { return type_; }
    std::int32_t selected() const { return selected_; }
    std::int32_t per_page() const { return per_page_; }
    int per_page_choice() const;
    // FUN_0068e790: the page's icons, first..last (inclusive; empty when last < first).
    std::int32_t first() const;
    std::int32_t last() const;
    int page_number() const;          // ISW_PageIndex_Label
    std::string range_text() const;   // ISW_PageRange_Label
    const IconCacheRange* range() const;

private:
    void update_range(); // UpdateRangeLabel's offset fix-up
    std::int32_t type_ = 1;       // +0x254 (the type combo starts on Spell)
    std::int32_t selected_ = -1;  // +0x258
    std::int32_t offset_ = 0;     // +0x25c
    std::int32_t per_page_ = 50;  // +0x264 (-1: All)
};

} // namespace hailstone::game
