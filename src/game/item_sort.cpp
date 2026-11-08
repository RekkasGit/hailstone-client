#include "game/item_sort.h"

#include <algorithm>
#include <array>
#include <map>
#include <tuple>

namespace hailstone::game {

using net::Item;
using net::ItemSlot;

std::vector<SortPosition> sort_positions(const Inventory& inv, std::int16_t type, const std::vector<std::int16_t>& slots) {
    std::vector<SortPosition> out;
    for (std::int16_t s : slots) {
        const Item* it = inv.at(ItemSlot{type, s, -1, -1, 0, 0});
        if (!it || !it->is_bag()) continue;
        for (int i = 0; i < it->bag_slots; ++i) out.push_back({ItemSlot{type, s, static_cast<std::int16_t>(i), -1, 0, 0}});
    }
    return out;
}

std::vector<SortPosition> bag_positions(const Inventory& inv, const ItemSlot& bag) {
    std::vector<SortPosition> out;
    const Item* it = inv.at(bag.parent());
    if (!it || !it->is_bag()) return out;
    for (int i = 0; i < it->bag_slots; ++i) {
        ItemSlot s = bag.parent();
        s.sub = static_cast<std::int16_t>(i);
        out.push_back({s});
    }
    return out;
}

bool sort_fits(const Inventory& inv, const SortPosition& pos, const Item& item) {
    if (pos.slot.sub < 0) return true;
    const Item* bag = inv.at(pos.slot.parent());
    return bag && bag->is_bag() && bag_fit(*bag, item) == 1;
}

namespace {

// Items that merge into each other (can_stack_with) share a kind; any other item is a kind of its own.
std::uint64_t kind_of(const Item& it, std::size_t token) {
    if (it.max_stack <= 1) return (1ull << 63) | token;
    return (static_cast<std::uint64_t>(it.id) << 16) | (static_cast<std::uint64_t>(it.attuned & 0xff) << 8) |
           (it.copied ? 1u : 0u);
}

} // namespace

ItemCategory item_category(const Item& it) {
    switch (it.item_type) {
    case 54: return ItemCategory::Augments;
    case 20: return ItemCategory::Spells; // scrolls
    case 14: case 15: case 38: return ItemCategory::Food; // food, drink, alcohol
    case 21: case 42: return ItemCategory::Potions; // potions, poisons
    case 0: case 1: case 2: case 3: case 4: case 5: case 7: case 19: case 27: case 35: case 45:
        return ItemCategory::Weapons; // melee, archery, throwing, arrows, 2H piercing, hand to hand
    case 8: case 10: case 29: case 52: return ItemCategory::Armor; // shields, armour, jewellery, charms
    default: break;
    }
    if (it.item_class == 2) return ItemCategory::Spells; // books and tomes
    if (it.tradeskills || it.item_type == 17) return ItemCategory::Tradeskill; // combinables
    return ItemCategory::None;
}

namespace {
constexpr std::array<std::string_view, 8> kCategoryNames{"", "food", "tradeskill", "spells", "weapons", "armor", "potions",
                                                          "augments"};
}

std::string_view category_name(ItemCategory c) { return kCategoryNames[static_cast<std::size_t>(c)]; }

ItemCategory category_by_name(std::string_view name) {
    for (std::size_t i = 1; i < kCategoryNames.size(); ++i)
        if (kCategoryNames[i] == name) return static_cast<ItemCategory>(i);
    return ItemCategory::None;
}

std::vector<SortClick> plan_tidy(const Inventory& inv, const std::vector<SortPosition>& positions) {
    std::vector<SortClick> clicks;
    std::vector<bool> full(positions.size());
    for (std::size_t p = 0; p < positions.size(); ++p) full[p] = inv.at(positions[p].slot) != nullptr;
    for (std::size_t p = 0; p < positions.size(); ++p) {
        const Item* it = inv.at(positions[p].slot);
        if (!it) continue;
        const ItemCategory c = item_category(*it);
        if (c == ItemCategory::None || positions[p].filter == c) continue;
        for (std::size_t q = 0; q < positions.size(); ++q)
            if (!full[q] && positions[q].filter == c && sort_fits(inv, positions[q], *it)) {
                clicks.push_back({positions[p].slot, 0});
                clicks.push_back({positions[q].slot, it->id});
                full[q] = true;
                full[p] = false;
                break;
            }
    }
    return clicks;
}

std::vector<SortClick> plan_stack(const Inventory& inv, const std::vector<SortPosition>& positions) {
    std::vector<SortClick> clicks;
    // the stacks of each item, in position order
    std::map<std::uint64_t, std::vector<std::size_t>> groups;
    std::vector<std::uint64_t> order;
    for (std::size_t p = 0; p < positions.size(); ++p) {
        const Item* it = inv.at(positions[p].slot);
        if (!it || !it->stackable()) continue;
        auto k = kind_of(*it, p);
        if (!groups.count(k)) order.push_back(k);
        groups[k].push_back(p);
    }
    for (auto k : order) {
        const auto& g = groups[k];
        if (g.size() < 2) continue;
        std::vector<std::uint32_t> count;
        for (std::size_t p : g) count.push_back(inv.at(positions[p].slot)->stack_count);
        const Item& first = *inv.at(positions[g.front()].slot);
        const std::uint32_t max = first.max_stack, id = first.id;
        std::size_t i = 0, j = g.size() - 1;
        while (i < j) {
            if (count[i] >= max) { ++i; continue; }
            if (count[j] == 0) { --j; continue; }
            const ItemSlot& to = positions[g[i]].slot;
            const ItemSlot& from = positions[g[j]].slot;
            clicks.push_back({from, 0});
            clicks.push_back({to, id}); // merge_stacks: as much as fits
            std::uint32_t moved = std::min(max - count[i], count[j]);
            count[i] += moved;
            count[j] -= moved;
            if (count[j] > 0) clicks.push_back({from, id}); // the rest back where it was
        }
    }
    return clicks;
}

std::optional<std::vector<SortClick>> plan_sort(const Inventory& inv, const std::vector<SortPosition>& positions) {
    const std::size_t n = positions.size();
    // tokens: the items, by where they are now
    std::vector<const Item*> item;
    std::vector<int> at(n, -1);
    for (std::size_t p = 0; p < n; ++p)
        if (const Item* it = inv.at(positions[p].slot)) {
            at[p] = static_cast<int>(item.size());
            item.push_back(it);
        }
    const std::size_t count = item.size();
    std::vector<std::size_t> sorted(count);
    for (std::size_t t = 0; t < count; ++t) sorted[t] = t;
    std::stable_sort(sorted.begin(), sorted.end(), [&](std::size_t a, std::size_t b) {
        const Item& x = *item[a];
        const Item& y = *item[b];
        return std::tie(x.item_type, x.name, x.id) < std::tie(y.item_type, y.name, y.id);
    });

    // the arrangement: each position takes the first item in order that fits there; a filtered bag's slots first,
    // from the items of its category, then the other slots, then what the filtered bags still have room for
    std::vector<int> want(n, -1); // the token whose kind goes at each position
    std::vector<bool> placed(count, false);
    for (int pass = 0; pass < 3; ++pass)
        for (std::size_t p = 0; p < n; ++p) {
            const ItemCategory filter = positions[p].filter;
            if (want[p] >= 0 || (pass == 1) == (filter != ItemCategory::None)) continue;
            for (std::size_t t : sorted)
                if (!placed[t] && (pass != 0 || item_category(*item[t]) == filter) &&
                    sort_fits(inv, positions[p], *item[t])) {
                    want[p] = static_cast<int>(t);
                    placed[t] = true;
                    break;
                }
        }
    for (std::size_t t = 0; t < count; ++t)
        if (!placed[t]) return std::nullopt;

    // where each item goes: a position already holding the kind it wants keeps its item (stacks of one item are
    // never dropped on each other: they would merge); the others take an item of that kind
    std::vector<std::uint64_t> kind(count);
    for (std::size_t t = 0; t < count; ++t) kind[t] = kind_of(*item[t], t);
    std::vector<int> dest(count, -1);
    std::vector<bool> fixed(n, false);
    for (std::size_t p = 0; p < n; ++p)
        if (want[p] >= 0 && at[p] >= 0 && kind[static_cast<std::size_t>(at[p])] == kind[static_cast<std::size_t>(want[p])]) {
            dest[static_cast<std::size_t>(at[p])] = static_cast<int>(p);
            fixed[p] = true;
        }
    for (std::size_t p = 0; p < n; ++p) {
        if (fixed[p] || want[p] < 0) continue;
        std::uint64_t k = kind[static_cast<std::size_t>(want[p])];
        for (std::size_t t = 0; t < count; ++t)
            if (dest[t] < 0 && kind[t] == k) {
                dest[t] = static_cast<int>(p);
                break;
            }
    }
    for (std::size_t t = 0; t < count; ++t)
        if (dest[t] < 0) return std::nullopt;

    // the moves: each item out of place is picked up and put down where it goes, which hands up the item there
    std::vector<SortClick> clicks;
    for (std::size_t p = 0; p < n; ++p) {
        int held = at[p];
        if (held < 0 || dest[static_cast<std::size_t>(held)] == static_cast<int>(p)) continue;
        clicks.push_back({positions[p].slot, 0});
        at[p] = -1;
        while (held >= 0) {
            auto d = static_cast<std::size_t>(dest[static_cast<std::size_t>(held)]);
            clicks.push_back({positions[d].slot, item[static_cast<std::size_t>(held)]->id});
            int there = at[d];
            at[d] = held;
            held = there;
        }
    }
    return clicks;
}

} // namespace hailstone::game
