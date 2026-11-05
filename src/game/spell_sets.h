#pragma once

// Spell sets ("spell loadouts", pinstSpellSets 0xe5c8b4): 10 named sets of the 12 gems' spells, kept in the character
// ini (<name>_<server>.ini, here hotbuttons.ini) as SaveSpellLoadoutsToIni @0x546020 writes them: section
// [SpellLoadouts], for each set marked changed SpellLoadout<n>.inuse (0 / 1) and, in use, SpellLoadout<n>.name and
// SpellLoadout<n>.slot1 .. .slot12 (spell ids, -1 for an empty gem), n from 1.
//
// Saving (CCastSpellWnd::WndNotification's text entry, SaveLoadout): the set of that name (case ignored), else the
// first unused one, takes the name and the gems' spells; then CEverQuest::SortSpellLoadouts @0x519960 sorts them
// (qsort with @0x519850: sets in use first, by name with __stricmp; unused after) and marks all 10 changed.
// /memspellset (Cmd_memspellset @0x4f7100) picks a set by number 1..10 or by the start of its name.
//
// edit() is Hailstone's own, not the original's: the spell book's set editor (ui/modern/spellbook.lua,
// ui/gamepad/spells.lua) renames a set and gives it spells without memorizing them first. The sets it leaves are
// ones SaveLoadout could have made (the same ini keys, sorted the same way), so the stock client reads them as its own.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace nmsc { class IniFile; }

namespace nmsc::game {

inline constexpr int kSpellLoadouts = 10;
inline constexpr int kLoadoutGems = 12;
inline constexpr std::size_t kLoadoutNameMax = 24; // the name entry's limit (CTextEntryWnd::Activate max 0x18)

struct SpellLoadout {
    std::array<std::int32_t, kLoadoutGems> spells; // +0: the gems' spell ids (-1 none)
    std::string name;                               // +0x30
    bool in_use = false;                            // +0x49
    bool changed = false;                           // +0x4a: written by the next save
    SpellLoadout() { spells.fill(-1); }
};

class SpellLoadouts {
public:
    // ReadCharacterIniSettings' part: the sets from [SpellLoadouts] (none changed).
    void load(const IniFile& ini);
    // SaveSpellLoadoutsToIni: the changed sets into [SpellLoadouts]; they are no longer changed.
    void save(IniFile& ini);
    // CEverQuest::SortSpellLoadouts: in use first, by name (case ignored); every set changed.
    void sort();

    enum class SaveResult { Saved, Full };
    // SaveLoadout: `name` (cut to kLoadoutNameMax) gets `gems`; sorted. `index` is where it is after the sort.
    SaveResult save_set(std::string_view name, const std::array<std::int32_t, kLoadoutGems>& gems, int* index = nullptr);
    // Hailstone's set editor: set n (in use) is renamed `name` (cut to kLoadoutNameMax; not empty) and given `gems`;
    // another set already of that name (case ignored) is replaced by it, as SaveLoadout replaces one. Sorted;
    // `index` is where it is after the sort. False when n is not in use or the name is empty.
    bool edit(int n, std::string_view name, const std::array<std::int32_t, kLoadoutGems>& gems, int* index = nullptr);
    // The delete confirmation's yes: the set is no longer in use; sorted (false when it was not in use).
    bool remove(int n);
    // Cmd_memspellset's choice: "1".."10" a set in use by number, else the first set in use whose name starts with
    // `arg` (case ignored); -1 none.
    int find(std::string_view arg) const;

    const SpellLoadout& at(int n) const { return sets_[static_cast<std::size_t>(n)]; }
    // Whether a set changed since the last call (the caller then saves).
    bool take_dirty() {
        bool d = dirty_;
        dirty_ = false;
        return d;
    }

private:
    int index_of(std::string_view name) const; // the set in use named exactly `name`, or -1
    std::array<SpellLoadout, kSpellLoadouts> sets_{};
    bool dirty_ = false;
};

} // namespace nmsc::game
