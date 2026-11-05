#include "game/tradeskill.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace nmsc::game {

bool is_combine_container(std::uint32_t bag_type) {
    return bag_type > 8 && bag_type < 0x3a && bag_type != 0x33 && bag_type != 0x35;
}

bool is_tradeskill_container_type(std::uint32_t t) {
    switch (t) {
    case 9: case 10: case 0xb: case 0xc: case 0xe: case 0xf: case 0x10: case 0x11: case 0x12: case 0x13: case 0x14:
    case 0x15: case 0x16: case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1f: case 0x20: case 0x21: case 0x22:
    case 0x23: case 0x24: case 0x26: case 0x27: case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d:
    case 0x2e: case 0x2f: case 0x30: case 0x31: case 0x32: case 0x34: case 0x36:
        return true;
    default:
        return false;
    }
}

namespace {

// GetContainerTradeskill's table (rof2decomp re/src/game/character/tradeskill_eh.cpp): the type, its tradeskill, and
// what the character must be (a class, a race, one of four classes) with the line said when not.
enum class Need : std::uint8_t { Nothing, Class, Race, AnyOfFourClasses };
struct ContainerEntry {
    std::uint8_t type, skill;
    Need need;
    std::uint16_t value, message;
};
constexpr ContainerEntry kContainerSkills[] = {
    {9, 59, Need::Nothing, 0, 0}, {10, 57, Need::Nothing, 0, 0}, {11, 58, Need::Nothing, 0, 0},
    {12, 56, Need::Nothing, 0, 0}, {14, 60, Need::Nothing, 0, 0}, {15, 60, Need::Nothing, 0, 0},
    {16, 61, Need::Nothing, 0, 0}, {17, 63, Need::Nothing, 0, 0}, {18, 64, Need::Nothing, 0, 0},
    {19, 65, Need::Nothing, 0, 0}, {20, 68, Need::Nothing, 0, 0}, {21, 69, Need::Nothing, 0, 0},
    {22, 69, Need::Nothing, 0, 0}, {24, 58, Need::Class, 12, 0x135}, {25, 58, Need::Class, 13, 0x136},
    {26, 58, Need::Class, 11, 0x137}, {27, 58, Need::Class, 14, 0x138}, {29, 58, Need::AnyOfFourClasses, 0, 0},
    {31, 63, Need::Race, 5, 0x139}, {32, 63, Need::Race, 6, 0x13a}, {33, 63, Need::Race, 10, 0x13b},
    {34, 63, Need::Race, 8, 0x13c}, {35, 63, Need::Race, 12, 0x13d}, {36, 63, Need::Race, 2, 0x13e},
    {38, 63, Need::Race, 0x80, 0x13f}, {39, 63, Need::Race, 1, 0x140}, {40, 63, Need::Race, 1, 0x140},
    {41, 61, Need::Race, 11, 0x141}, {42, 61, Need::Race, 3, 0x142}, {43, 61, Need::Race, 4, 0x143},
    {44, 64, Need::Race, 4, 0x144}, {45, 69, Need::Race, 0x80, 0x145}, {46, 55, Need::Nothing, 0, 0},
    {47, 63, Need::Race, 9, 0x146}, {48, 63, Need::Race, 4, 0x147}, {49, 63, Need::Race, 11, 0x148},
    {50, 63, Need::Race, 3, 0x149}, {52, 63, Need::Race, 0x14a, 0x1698}, {54, 60, Need::Nothing, 0, 0},
};

// The race's bit index (1..17) in an item's race mask.
std::int32_t race_index(std::int32_t race) {
    if (race < 13) return race;
    switch (race) {
    case 0x14a: return 15;
    case 0x80: return 13;
    case 0x82: return 14;
    case 0x20a: return 16;
    default: return 17;
    }
}

std::uint32_t bit(std::int32_t index) { return 1u << ((index - 1) & 31); }

} // namespace

ContainerSkill container_tradeskill(std::uint32_t bag_type, std::uint32_t classes, std::uint32_t deities,
                                    std::uint32_t races, const ContainerUser& u) {
    ContainerSkill r;
    bool quiet = bag_type == 13 || bag_type == 30;
    auto refuse = [&](std::uint32_t message) {
        if (!quiet) r.message = message;
        return r;
    };
    if (classes != 0 && !(bit(u.class_id) & classes)) return refuse(5942);
    auto is = [&](std::uint32_t cls) {
        return u.research_classes != 0 ? (u.research_classes & bit(static_cast<std::int32_t>(cls))) != 0
                                       : u.class_id == cls;
    };
    if (deities != 0 && !(bit(u.deity == 396 ? 1 : u.deity - 199) & deities)) return refuse(5943);
    if (races != 0 && !(bit(race_index(u.race)) & races)) return refuse(5944);
    for (const ContainerEntry& e : kContainerSkills) {
        if (e.type != bag_type) continue;
        bool allowed = true;
        switch (e.need) {
        case Need::Nothing: break;
        case Need::Class: allowed = is(e.value); break;
        case Need::Race: allowed = u.race == static_cast<std::int32_t>(e.value); break;
        case Need::AnyOfFourClasses: allowed = is(12) || is(13) || is(11) || is(14); break;
        }
        if (!allowed) {
            r.message = e.message;
            return r;
        }
        r.skill = e.skill;
        break;
    }
    return r;
}

bool opens_in_tradeskill_window(const OpenContainerInput& in, const std::function<bool()>& has_skill) {
    if (!in.tradeskill || in.tradeskill_window_active || !in.top_level_or_world) return false;
    // IsTradeskillContainerType, then GetContainerTradeskill and HasSkill whatever the type (a refusal is said).
    bool type = is_tradeskill_container_type(in.bag_type);
    bool skill = has_skill();
    return type && skill;
}

const net::RecipeReply* TradeskillState::selected_recipe() const {
    for (const auto& r : recipes)
        if (r.recipe_id == selected) return &r;
    return nullptr;
}

void TradeskillState::add_recipe(const net::RecipeReply& r) {
    if (r.object_type != object_type || r.container_id != container_id) return;
    for (const auto& have : recipes)
        if (have.recipe_id == r.recipe_id) return;
    recipes.push_back(r);
}

RecipeFavorites parse_recipe_favorites(std::string_view value) {
    RecipeFavorites f{};
    if (value.empty() || value[0] == '*') return f;
    std::size_t at = 0;
    for (std::uint32_t& place : f) {
        if (at < value.size() && value[at] == '0') {
            place = 0;
            continue;
        }
        // GetDelimitedToken(.., ',', 1, 0): the text up to the comma, which is consumed too.
        std::size_t end = value.find(',', at);
        std::string_view token = value.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
        at = end == std::string_view::npos ? value.size() : end + 1;
        std::string t(token);
        if (t.size() > 1 && t[0] == '0' && t[1] == 'x')
            place = static_cast<std::uint32_t>(std::strtol(t.c_str(), nullptr, 16));
        else
            place = static_cast<std::uint32_t>(std::atoi(t.c_str()));
    }
    return f;
}

std::string format_recipe_favorites(const RecipeFavorites& favorites) {
    std::string s;
    for (std::uint32_t r : favorites)
        if (r != 0) s += std::to_string(static_cast<std::int32_t>(r)) + ",";
    return s;
}

bool add_recipe_favorite(RecipeFavorites& f, std::uint32_t id) {
    for (std::uint32_t& r : f)
        if (r == 0) {
            r = id;
            return true;
        }
    return false;
}

bool remove_recipe_favorite(RecipeFavorites& f, std::uint32_t id) {
    bool any = false;
    for (std::uint32_t& r : f)
        if (r == id) {
            r = 0;
            any = true;
        }
    return any;
}

namespace {

// The general slots' items and, inside the bags among them, theirs: (slot, item).
template <class F>
void for_each_carried(const Inventory& inv, F&& f) {
    for (std::int16_t s = net::kSlotGeneral1; s <= net::kSlotGeneral10; ++s) {
        const net::Item* top = inv.at(net::ItemSlot::possessions(s));
        if (!top) continue;
        f(net::ItemSlot::possessions(s), *top);
        for (const net::BagItem& b : top->contents)
            f(net::ItemSlot::possessions(s, static_cast<std::int16_t>(b.index)), b.item);
    }
}

std::uint32_t units(const net::Item& it) { return it.stackable() ? it.stack_count : 1; }

} // namespace

std::uint32_t carried_count(const Inventory& inventory, std::uint32_t item_id) {
    std::uint32_t n = 0;
    for_each_carried(inventory, [&](const net::ItemSlot&, const net::Item& it) {
        if (it.id == item_id) n += units(it);
    });
    return n;
}

RecipeNeeds recipe_needs(const net::RecipeDetails& details, std::uint32_t component_count, const Inventory& inventory) {
    RecipeNeeds r;
    for (const net::RecipeComponent& c : details.components) {
        auto at = std::find_if(r.components.begin(), r.components.end(),
                               [&](const ComponentNeed& n) { return n.item_id == c.item_id; });
        if (at != r.components.end()) {
            ++at->need;
            continue;
        }
        ComponentNeed n;
        n.item_id = c.item_id;
        n.icon = c.icon;
        n.name = c.name;
        n.need = 1;
        n.have = carried_count(inventory, c.item_id);
        r.components.push_back(std::move(n));
    }
    r.complete = component_count <= details.components.size();
    if (r.components.empty()) return r;
    std::uint32_t most = UINT32_MAX;
    for (const ComponentNeed& n : r.components) most = std::min(most, n.have / n.need);
    r.makeable = most;
    return r;
}

bool has_room_for_result(const Inventory& inventory, const std::function<bool(int)>& slot_available,
                         std::uint32_t result_id) {
    for (std::int16_t s = net::kSlotGeneral1; s <= net::kSlotGeneral10; ++s) {
        const net::Item* top = inventory.at(net::ItemSlot::possessions(s));
        if (!top) {
            if (slot_available(s)) return true;
            continue;
        }
        if (top->is_bag())
            for (std::int16_t i = 0; i < top->bag_slots; ++i)
                if (!inventory.at(net::ItemSlot::possessions(s, i))) return true;
    }
    bool stack = false;
    if (result_id)
        for_each_carried(inventory, [&](const net::ItemSlot&, const net::Item& it) {
            if (it.id == result_id && it.stackable() && it.stack_count < it.max_stack) stack = true;
        });
    return stack;
}

std::optional<PutAway> put_away_slot(const Inventory& inventory, const net::Item& held,
                                     const std::function<bool(int)>& slot_available) {
    if (held.stackable()) {
        std::optional<PutAway> merge;
        for_each_carried(inventory, [&](const net::ItemSlot& slot, const net::Item& it) {
            if (!merge && it.id == held.id && it.stack_count < it.max_stack && can_stack_with(held, it))
                merge = PutAway{slot, true};
        });
        if (merge) return merge;
    }
    for (std::int16_t s = net::kSlotGeneral1; s <= net::kSlotGeneral10; ++s)
        if (!inventory.at(net::ItemSlot::possessions(s)) && slot_available(s))
            return PutAway{net::ItemSlot::possessions(s), false};
    for (std::int16_t s = net::kSlotGeneral1; s <= net::kSlotGeneral10; ++s) {
        const net::Item* bag = inventory.at(net::ItemSlot::possessions(s));
        if (!bag || !bag->is_bag() || bag_fit(*bag, held) != 1) continue;
        for (std::int16_t i = 0; i < bag->bag_slots; ++i)
            if (!inventory.at(net::ItemSlot::possessions(s, i))) return PutAway{net::ItemSlot::possessions(s, i), false};
    }
    return std::nullopt;
}

std::uint32_t recipe_trivial_color(std::int32_t trivial, std::int32_t skill) {
    static constexpr std::int32_t kAbove[] = {0, 10, 25, 50, 500};
    static constexpr std::uint32_t kColor[] = {0xff00ff00, 0xff8aa3ff, 0xff0000ff, 0xffffff00, 0xffff0000};
    for (int i = 0; i < 5; ++i)
        if (trivial - skill <= kAbove[i]) return kColor[i];
    return 0xffff0000;
}

} // namespace nmsc::game
