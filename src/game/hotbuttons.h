#pragma once

// The hot buttons as the original keeps them: 10 hot bars of 10 pages of 12 buttons (the table at 0xe3ce08,
// 0x6c bytes per button), saved per character in <name>_<server>.ini: section [HotButtons] for bar 1 and
// [HotButtons<n>] for bar n, key Page<p>Button<b>, value "%c%d,%c%d,%s,%d,%s" = type + 'A', slot, icon type +
// 'A', icon slot, item guid, item id, label (FormatHotButtonIniString @0x5464e0, ParseHotButtonIniString
// @0x546da0, SaveHotButtonsToIni @0x546b40, ReadCharacterIniSettings @0x546ea0). The same file holds the socials
// ([Socials] Page<p>Button<b>Name / Color / Line1..5) and the Actions window's ability buttons ([Abilities]
// Ability1..6, [CombatSkills] CombatSkill1..4).
//
// Button types (CHotButtonWnd::DoHotButton @0x68c8b0): 1 weapon slot (slot 0 melee attack, 1 ranged), 2 combat
// skill and 3 ability (UseSkill), 4 social, 5 inventory slot, 6 menu button, 7 spell gem, 8 pet command, 9 skill
// (UseSkill), 10 combat ability (discipline), 11 leadership, 12 item link, 13 krono slot.

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "game/lua_social.h"

namespace hailstone { class IniFile; }

namespace hailstone::game {

inline constexpr int kHotBars = 10, kHotPages = 10, kHotButtons = 12;
inline constexpr int kSocialPages = 10, kSocialButtons = 12, kSocialLines = 5;

enum class HotButtonType : std::uint8_t {
    None = 0, WeaponSlot = 1, CombatSkill = 2, Ability = 3, Social = 4, InventorySlot = 5, MenuButton = 6,
    SpellGem = 7, PetCommand = 8, Skill = 9, CombatAbility = 10, Leadership = 11, ItemLink = 12, KronoSlot = 13,
};

// The menu buttons of type 6 (DoHotButton's switch): who, invite/follow, disband, camp, sit, run/walk, (6 unused),
// options, toggle view, inventory, spell book, (11), find.
enum class MenuButton : std::int32_t {
    Who = 0, InviteFollow = 1, Disband = 2, Camp = 3, Sit = 4, RunWalk = 5, Options = 7, ToggleView = 8,
    Inventory = 9, SpellBook = 10, Find = 12,
};

struct HotButton {
    HotButtonType type = HotButtonType::None;
    std::int32_t slot = 0;       // +4: skill id, gem 0..11, social index, menu button, inventory slot ...
    std::int32_t icon_type = -1; // +0x64
    std::int32_t icon_slot = -1; // +0x68
    std::string guid = "0000000000000000"; // +0xc EqItemGuid (item links)
    std::int32_t item_id = 0;    // +0x20
    std::string label;           // +0x24, up to 63 characters
    bool empty() const { return type == HotButtonType::None; }
};

struct Social {
    std::string name;  // up to 15 characters
    std::int32_t color = 0;
    std::array<std::string, kSocialLines> lines;
    // The /timer cooldown (+0x514 start, +0x518 length, ms): display only, never saved.
    std::uint32_t cooldown_start = 0, cooldown_length = 0;
    // Hailstone: the social's Lua (game/lua_social.h), run instead of the lines; kept in socials/<n>.lua.
    LuaSocial lua;
    bool empty() const { return name.empty(); }
    bool is_lua() const { return !lua.empty(); }
};

// "%c%d,%c%d,%s,%d,%s" and back.
std::string format_hot_button(const HotButton& b);
HotButton parse_hot_button(const std::string& text);

inline constexpr int kCombatAbilityButtons = 8;
inline constexpr std::int32_t kCombatAbilityValueBase = 0x84; // a combat ability's hotbutton / cursor value - index

class HotButtonStore {
public:
    HotButton& at(int bar, int page, int button);
    const HotButton& at(int bar, int page, int button) const;
    // The page a bar shows (0xe3cdfe; 0-based).
    int page(int bar) const { return pages_[static_cast<std::size_t>(bar)]; }
    void set_page(int bar, int page);
    HotButton& current(int bar, int button) { return at(bar, page(bar), button); }

    Social& social(int index) { return socials_[static_cast<std::size_t>(index)]; }
    const Social& social(int index) const { return socials_[static_cast<std::size_t>(index)]; }
    // The Actions window's buttons ([Abilities] Ability1..6, [CombatSkills] CombatSkill1..4; -1 empty).
    std::array<std::int32_t, 6> abilities{-1, -1, -1, -1, -1, -1};
    std::array<std::int32_t, 4> combat_skills{-1, -1, -1, -1};
    // The combat ability window's buttons ([CombatAbilities] CombatAbility1..8, 0xe15e78): 0x84 + the profile's
    // combat ability index (as a type 10 hotbutton's value), -1 empty.
    std::array<std::int32_t, kCombatAbilityButtons> combat_abilities{-1, -1, -1, -1, -1, -1, -1, -1};

    void load(const IniFile& ini);
    void save(IniFile& ini) const;
    // The Lua socials' files in `dir` (socials/ beside hotbuttons.ini, game/lua_social.h), after load(): a file's
    // name and colour fill a social hotbuttons.ini does not name. save_lua writes a file for each Lua social and
    // removes the files of the others.
    void load_lua(const std::filesystem::path& dir);
    bool save_lua(const std::filesystem::path& dir) const;

private:
    std::array<HotButton, kHotBars * kHotPages * kHotButtons> buttons_{};
    std::array<int, kHotBars> pages_{};
    std::array<Social, kSocialPages * kSocialButtons> socials_{};
};

// FUN_004ea9f0 @0x4ea9f0 (SocialStore): a social (its name cut to 15 characters, its first line to 255, the colour)
// in the first empty slot from `start` (0..119, else 0), or with `same_name` the first with that name (stricmp); the
// other lines are left as they were and the cooldown cleared. Returns its index or -1 (all 120 in use). The original
// then puts it on the cursor when `start` was 0 (or out of range).
int store_social(HotButtonStore& store, std::string_view name, std::string_view line, std::int32_t color,
                 std::int32_t start, bool same_name);

// The section of a bar: "HotButtons", "HotButtons2" ... "HotButtons10".
std::string hot_button_section(int bar);

} // namespace hailstone::game
