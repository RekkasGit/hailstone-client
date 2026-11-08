#include "game/combat_ability_menu.h"

#include <cstdio>
#include <cstring>

namespace hailstone::game {

namespace {

// The name as the original keeps it: strncpy into 63 characters, then a terminator.
std::string menu_name(const std::function<std::string(std::int32_t)>& name_of, std::int32_t id) {
    std::string n = name_of ? name_of(id) : std::string();
    if (n.size() > 63) n.resize(63);
    if (auto z = n.find('\0'); z != std::string::npos) n.resize(z);
    return n;
}

// The group with `id` among `groups`, else a new one at the first place whose name compares greater (strcmp).
CombatAbilityMenuGroup& find_or_insert(std::vector<CombatAbilityMenuGroup>& groups, std::int32_t id,
                                       const std::function<std::string(std::int32_t)>& name_of, bool& fresh) {
    const std::string name = menu_name(name_of, id);
    std::size_t at = groups.size();
    bool placed = false;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        if (groups[i].id == id) {
            fresh = false;
            return groups[i];
        }
        if (!placed && std::strcmp(name.c_str(), groups[i].name.c_str()) < 0) {
            at = i;
            placed = true;
        }
    }
    CombatAbilityMenuGroup g;
    g.id = id;
    g.name = name;
    fresh = true;
    return *groups.insert(groups.begin() + static_cast<std::ptrdiff_t>(at), std::move(g));
}

// CCombatAbilityWnd_CategorizeAbility @0x65a890.
void categorize(std::vector<CombatAbilityMenuGroup>& menu, const CombatAbilityEntry& a, std::int32_t sub,
                const std::function<std::string(std::int32_t)>& name_of) {
    CombatAbilityMenuItem item;
    item.index = a.index;
    item.level = a.level;
    char text[0x80];
    std::snprintf(text, sizeof text, "%02d - %s", a.level, a.name.c_str());
    item.text = text;
    bool fresh = false;
    CombatAbilityMenuGroup& cat = find_or_insert(menu, a.category, name_of, fresh);
    CombatAbilityMenuGroup& group = find_or_insert(cat.children, sub, name_of, fresh);
    if (fresh) {
        group.items.push_back(std::move(item));
        return;
    }
    for (std::size_t k = 0; k < group.items.size(); ++k)
        if (group.items[k].level < item.level) {
            group.items.insert(group.items.begin() + static_cast<std::ptrdiff_t>(k), std::move(item));
            return;
        }
    group.items.push_back(std::move(item));
}

} // namespace

std::vector<CombatAbilityMenuGroup> build_combat_ability_menu(
    const std::vector<CombatAbilityEntry>& abilities, const std::function<std::string(std::int32_t)>& category_name) {
    std::vector<CombatAbilityMenuGroup> menu;
    for (const auto& a : abilities) {
        if (a.category == 0) continue;
        if (a.subcategory != 0) categorize(menu, a, a.subcategory, category_name);
        if (a.subcategory2 != 0) categorize(menu, a, a.subcategory2, category_name);
    }
    return menu;
}

} // namespace hailstone::game
