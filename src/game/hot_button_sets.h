#pragma once

// Hotbutton sets ("HB loadouts", the table at 0xe5cbac: 10 entries of 0x20 bytes, name +0 (0x19 bytes), in use +0x19,
// changed +0x1a, file number +0x1c) and the hot bar window's own menu (CHotButtonWnd::sub_0068a1c0 builds it,
// sub_0068abd0 fits it before it pops up, WndNotification @0x68c190 and HotButtons_LoadFromIniFile @0x68af20 answer it):
//
//   6643 Window                    the window's default menu (this client's window menu)
//   8009 Load hotbutton set   ▸    the sets in use (ids 0x17d4 + n), disabled with none: ConfirmationDialog 0x80
//                                  (8030, Yes / No, no time limit), Yes loads set n into the bar's 10 pages
//   8010 Save hotbutton set        CTextEntryWnd "Enter hotbutton set name:" (8008; 1..24 characters, empty)
//   8011 Delete hotbutton set ▸    the sets in use (ids 0x1838 + n), disabled with none: ConfirmationDialog 0x81 (8031)
//   8028 Clear current hotbuttons  ConfirmationDialog 0x82 (8029): every button of the bar's 10 pages cleared, 8032
//   7674 Button Size               CQuantityWnd "Percent" 40..175 from the bar's PercentSize (FUN_0068a170)
//   7673 Show Key Map              checked with the bar's ShowKeyMap: toggles it
//   7675 Show Spinner              checked with the bar's ShowSpinner: toggles it (the page buttons)
//   4654 Open New Bar              enabled while a bar is hidden: shows the first hidden bar (FUN_0068b6f0)
//
// Saving (the text entry's OK, notification 0x26): the set of that name (case ignored) among those in use, else the
// first unused entry; none: eqstr 8006 with "100" (the original's text, for 10 sets), colour 0xd. The entry takes the
// name, in use, changed, file number = its index (a change here, below), and the bar's 10 pages of 12 buttons go to
// .\userdata\HB_<file number>_<name>_<server>.ini, section [Hotbuttons], key Page<p>.Slot<b> (1-based): the
// button's FormatHotButtonIniString text, an empty button's key removed; then eqstr 8007 with the name (colour 0xf)
// and FUN_005199c0: the sets sorted (qsort with FUN_00519890: in use first, by name with __stricmp), all marked
// changed, SaveHotButtonLoadoutsToIni. The file number stays the index the set was saved at, while the sort moves it,
// so in the original a later set saved at that index (a new one after a delete, or another set re-saved there) writes
// over the file of the set moved away and that set is lost. Fixed here (a change): a set saved again keeps its own
// file number, and a new set takes the lowest number no other set in use has; a set sharing its file with another
// (an ini the original wrote after the loss) is given such a number when saved. [HBLoadouts] and HB_<n>.ini keep the
// original's format, so the original's files load as they are.
// Loading (0x80, Yes): set n in use: for each page and button the button is cleared (ClearHotButtonSlot) and, when
// the file has a value for it, parsed (ParseHotButtonIniString); saved (SaveHotButtonsToIni), eqstr 8015 (colour 0xf).
// Deleting (0x81, Yes): set n in use: eqstr 8013, no longer in use, changed, FUN_005199c0 (sorted, saved); the file
// stays. Clearing (0x82, Yes): the bar's 120 buttons cleared, saved, eqstr 8032 (colour 0xf).
// The character ini keeps them (SaveHotButtonLoadoutsToIni @0x546160, ReadCharacterIniSettings @0x546ea0): section
// [HBLoadouts], for each changed entry HBSet<n>.inuse (0 / 1) and, in use, HBSet<n>.name and HBSet<n>.fileNum.
// (The original's save loop runs 100 times over the 10 entries and whatever follows them; only the 10 are kept here.)

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "game/hotbuttons.h"

namespace nmsc { class IniFile; }

namespace nmsc::game {

inline constexpr int kHotButtonSets = 10;
inline constexpr std::size_t kHotButtonSetNameMax = 24; // CTextEntryWnd::Activate(.., 1, 0x18, "")
inline constexpr std::uint32_t kStrHbWindow = 6643, kStrHbLoadSet = 8009, kStrHbSaveSet = 8010,
                               kStrHbDeleteSet = 8011, kStrHbClearAll = 8028, kStrHbButtonSize = 7674,
                               kStrHbShowKeyMap = 7673, kStrHbShowSpinner = 7675, kStrHbOpenNewBar = 4654,
                               kStrHbSetLimit = 8006, kStrHbSetSaved = 8007, kStrHbEnterSetName = 8008,
                               kStrHbSetLoaded = 8015, kStrHbSetDeleted = 8013, kStrHbAskLoad = 8030,
                               kStrHbAskDelete = 8031, kStrHbAskClear = 8029, kStrHbCleared = 8032;
// The bar's button size (CHotButtonWnd +0x29c, the UI ini's PercentSize; CQuantityWnd::Open(.., max 175, .., min 40)).
inline constexpr int kHotButtonPercentMin = 40, kHotButtonPercentMax = 175, kHotButtonPercentDefault = 100;
// CHotButton::SetButtonSize @0x550120: a size of the button (its XML size, decal, item slot) at `percent`.
inline int hot_button_scaled(int base, int percent) { return base * percent / 100; }

struct HotButtonSet {
    std::string name;      // +0
    bool in_use = false;   // +0x19
    bool changed = false;  // +0x1a: written by the next save
    std::int32_t file = 0; // +0x1c: the HB_<file>_... file
};

class HotButtonSets {
public:
    // ReadCharacterIniSettings' part: [HBLoadouts] (none changed).
    void load(const IniFile& ini);
    // SaveHotButtonLoadoutsToIni: the changed entries into [HBLoadouts]; they are no longer changed.
    void save(IniFile& ini);
    // FUN_005199c0 without its save: in use first, by name (case ignored); every entry changed.
    void sort();
    // The text entry's OK: the entry `name` (cut to kHotButtonSetNameMax) is saved as (the set of that name, else the
    // first unused one; -1: all 10 in use), marked in use and changed. Its file number (changed from the original's
    // "that index"): a set saved again keeps its own unless another set in use shares it; otherwise, and for a new
    // set, the lowest number no other set in use has. Not sorted.
    int begin_save(std::string_view name);
    // The delete confirmation's yes: entry n no longer in use, sorted (false when it was not in use).
    bool remove(int n);
    int in_use_count() const;

    const HotButtonSet& at(int n) const { return sets_[static_cast<std::size_t>(n)]; }
    bool take_dirty() {
        bool d = dirty_;
        dirty_ = false;
        return d;
    }

private:
    std::array<HotButtonSet, kHotButtonSets> sets_{};
    bool dirty_ = false;
};

// The set file of a bar: [Hotbuttons] Page<p>.Slot<b> for its 10 pages of 12 buttons (an empty button's key removed).
void write_hot_button_set(IniFile& file, const HotButtonStore& store, int bar);
// HotButtons_LoadFromIniFile's 0x80: each button of the bar cleared, then parsed from the file's value when it has one.
void read_hot_button_set(const IniFile& file, HotButtonStore& store, int bar);
// HotButtons_LoadFromIniFile's 0x82: every button of the bar's 10 pages cleared (ClearHotButtonSlot).
void clear_hot_bar(HotButtonStore& store, int bar);
// The set file's name in the character's folder (the original's userdata\HB_<n>_<name>_<server>.ini).
std::string hot_button_set_file(std::int32_t file);

} // namespace nmsc::game
