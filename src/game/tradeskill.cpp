#include "game/tradeskill.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iterator>

#include "core/msvc_atoi.h"

namespace hailstone::game {

bool is_combine_container(std::uint32_t bag_type) {
    return bag_type > 8 && bag_type < 0x3a && bag_type != 0x33 && bag_type != 0x35;
}

bool is_tradeskill_container_type(std::uint32_t t) {
    // Bag types 9..54, without the few in that range that are not tradeskill containers.
    if (t < 9 || t > 54) return false;
    switch (t) {
    case 13: case 23: case 28: case 29: case 30: case 37: case 51: case 53:
        return false;
    default:
        return true;
    }
}

namespace {

// GetContainerTradeskill's container types, in three kinds: open to anyone, a research container of one of the four
// caster classes, or kept to one race (with the line said to anyone else).
struct OpenBench {
    std::uint8_t type, skill;
};
constexpr OpenBench kOpenBenches[] = {
    {12, 56}, {46, 55}, {10, 57}, {11, 58}, {9, 59},  {14, 60}, {15, 60}, {54, 60},
    {16, 61}, {17, 63}, {18, 64}, {19, 65}, {20, 68}, {21, 69}, {22, 69},
};

// Research (skill 58): type 24 + i for class kResearchClasses[i] (refused with eqstr 0x135 + i), and type 29 for any
// of them (refused without a line).
constexpr std::int32_t kResearchSkill = 58;
constexpr std::uint32_t kResearchClasses[4] = {12, 13, 11, 14};
constexpr std::uint32_t kAnyResearchType = 29;

struct RacialBench {
    std::uint8_t type, skill;
    std::int32_t race;
    std::uint16_t refusal; // eqstr
};
constexpr RacialBench kRacialBenches[] = {
    {39, 63, 1, 0x140},  {40, 63, 1, 0x140},  {36, 63, 2, 0x13e},   {50, 63, 3, 0x149},    {42, 61, 3, 0x142},
    {43, 61, 4, 0x143},  {44, 64, 4, 0x144},  {48, 63, 4, 0x147},   {31, 63, 5, 0x139},    {32, 63, 6, 0x13a},
    {34, 63, 8, 0x13c},  {47, 63, 9, 0x146},  {33, 63, 10, 0x13b},  {41, 61, 11, 0x141},   {49, 63, 11, 0x148},
    {35, 63, 12, 0x13d}, {38, 63, 0x80, 0x13f}, {45, 69, 0x80, 0x145}, {52, 63, 0x14a, 0x1698},
};

// The race's bit index (1..17) in an item's race mask: races below 13 are their own index, four later races follow,
// and every other race shares the last bit.
std::int32_t race_index(std::int32_t race) {
    if (race < 13) return race;
    struct LaterRace {
        std::int32_t race, index;
    };
    static constexpr LaterRace kLater[] = {{0x80, 13}, {0x82, 14}, {0x14a, 15}, {0x20a, 16}};
    for (const LaterRace& l : kLater)
        if (l.race == race) return l.index;
    return 17;
}

std::uint32_t bit(std::int32_t index) { return std::uint32_t{1} << (static_cast<std::uint32_t>(index - 1) % 32u); }

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
    // From here a refusal's line is set even for the quiet types.
    auto grant = [&](std::int32_t skill, bool allowed, std::uint32_t refusal) {
        if (allowed)
            r.skill = skill;
        else
            r.message = refusal;
        return r;
    };
    for (const OpenBench& b : kOpenBenches)
        if (b.type == bag_type) return grant(b.skill, true, 0);
    if (bag_type >= 24 && bag_type < 24 + std::size(kResearchClasses)) {
        const std::uint32_t i = bag_type - 24;
        return grant(kResearchSkill, is(kResearchClasses[i]), 0x135 + i);
    }
    if (bag_type == kAnyResearchType) {
        bool any = false;
        for (std::uint32_t cls : kResearchClasses) any = any || is(cls);
        return grant(kResearchSkill, any, 0);
    }
    for (const RacialBench& b : kRacialBenches)
        if (b.type == bag_type) return grant(b.skill, u.race == b.race, b.refusal);
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
            place = static_cast<std::uint32_t>(core::msvc_atoi(t));
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

} // namespace hailstone::game
