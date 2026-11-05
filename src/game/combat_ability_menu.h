#pragma once

// The combat ability window's right-click menu (CCombatAbilityWnd_RebuildAbilityMenus @0x65ae70 and
// CCombatAbilityWnd_CategorizeAbility @0x65a890): the character's combat abilities filed by spell category and
// subcategory (dbstr type 5 names). Each ability goes under (Category, Subcategory) and (Category, Subcategory2),
// either one only when not 0, and none with Category 0. Categories and the subcategories of each are kept in
// strcmp order of their names (a new one before the first whose name compares greater, else last); the abilities
// of a subcategory by level, highest first (a new one before the first of a lower level, else last). A menu item
// reads "%02d - %s" (level, name) and has id 10000 + the ability's index.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nmsc::game {

struct CombatAbilityEntry {
    std::int32_t index = 0; // the profile's combat ability n (PcBase::GetCombatAbility)
    std::int32_t category = 0, subcategory = 0, subcategory2 = 0;
    std::uint8_t level = 0; // EQ_Spell::GetSpellLevelNeeded(the profile's class)
    std::string name;
};

struct CombatAbilityMenuItem {
    std::int32_t index = 0;
    std::uint8_t level = 0;
    std::string text; // "%02d - %s"
};
struct CombatAbilityMenuGroup {
    std::int32_t id = 0;
    std::string name; // CDBStr::GetString(id, 5), at most 63 characters
    std::vector<CombatAbilityMenuItem> items;     // a subcategory's abilities
    std::vector<CombatAbilityMenuGroup> children; // a category's subcategories
};

inline constexpr std::int32_t kCombatAbilityMenuIdBase = 10000;

// The menu for the abilities in index order (0..299); `category_name(id)` is the dbstr type 5 string. Empty: no
// menu (the window's right click then does nothing).
std::vector<CombatAbilityMenuGroup> build_combat_ability_menu(
    const std::vector<CombatAbilityEntry>& abilities, const std::function<std::string(std::int32_t)>& category_name);

} // namespace nmsc::game
