#include "game/bag_move.h"

#include <map>
#include <optional>

#include "game/inventory.h"

namespace nmsc::game {

namespace {

struct Entry {
    BagMove move;
    net::Item item; // the item moved, with move.count as its stack count
    bool placed = false;
};

const net::Item* child(const net::Item& bag, int sub) {
    for (const auto& c : bag.contents)
        if (static_cast<int>(c.index) == sub) return &c.item;
    return nullptr;
}

} // namespace

BagMovePlan plan_bag_move(const net::Item& cursor, const net::Item& target, bool force_bag) {
    BagMovePlan plan;
    std::vector<Entry> list;
    auto add = [&](BagMove::Where from, int sub, const net::Item& item, bool to_cursor_bag) {
        Entry e;
        e.move.from = from;
        e.move.from_sub = sub;
        e.move.to_cursor_bag = to_cursor_bag;
        e.move.count = item.stack_count;
        e.item = item;
        list.push_back(std::move(e));
    };
    if (cursor.is_bag() && (force_bag || !cursor.contents.empty())) {
        for (int s = 0; s < cursor.bag_slots; ++s)
            if (const net::Item* it = child(cursor, s)) add(BagMove::Where::CursorBag, s, *it, false);
        if (force_bag)
            for (int s = 0; s < target.bag_slots; ++s)
                if (const net::Item* it = child(target, s)) add(BagMove::Where::TargetBag, s, *it, true);
    } else {
        add(BagMove::Where::Cursor, -1, cursor, false);
    }
    if (list.empty()) return plan;
    const std::size_t sources = list.size();

    // DetachSourceItems: what stays in each bag once every source is out.
    auto left_in = [&](bool cursor_bag, int s) -> const net::Item* {
        const net::Item& bag = cursor_bag ? cursor : target;
        const net::Item* it = child(bag, s);
        if (!it) return nullptr;
        BagMove::Where w = cursor_bag ? BagMove::Where::CursorBag : BagMove::Where::TargetBag;
        for (std::size_t i = 0; i < sources; ++i)
            if (list[i].move.from == w && list[i].move.from_sub == s) return nullptr;
        return it;
    };
    auto taken = [&](bool cursor_bag, int s) {
        for (const Entry& e : list)
            if (e.placed && e.move.to_cursor_bag == cursor_bag && e.move.to_sub == s) return true;
        return false;
    };

    for (std::size_t i = 0; i < list.size(); ++i) {
        const bool to_cursor = list[i].move.to_cursor_bag;
        const net::Item& bag = to_cursor ? cursor : target;
        const bool consents = bag_fit(bag, list[i].item) == 1; // ItemClient::CanGoInBag
        bool found = false;
        if (consents && list[i].item.stackable()) {
            // FUN_005713d0 over the bag's slots.
            for (int s = 0; s < bag.bag_slots && !found; ++s) {
                const net::Item* there = left_in(to_cursor, s);
                if (!there || !can_stack_with(list[i].item, *there)) continue;
                const auto max = static_cast<std::int64_t>(list[i].item.max_stack);
                std::int64_t total = there->stack_count;
                if (total < max)
                    for (const Entry& e : list)
                        if (e.placed && e.move.to_cursor_bag == to_cursor && e.move.to_sub == s &&
                            can_stack_with(list[i].item, e.item))
                            total += e.move.count;
                if (total == 0) continue;
                const std::int64_t count = list[i].move.count;
                if (count + total <= max) {
                    list[i].move.to_sub = s;
                    list[i].placed = true;
                    found = true;
                } else if (total < max) {
                    Entry rest = list[i];
                    rest.move.count = static_cast<std::uint32_t>(count - (max - total));
                    rest.item.stack_count = rest.move.count;
                    rest.placed = false;
                    rest.move.to_sub = -1;
                    list[i].move.count = static_cast<std::uint32_t>(max - total);
                    list[i].item.stack_count = list[i].move.count;
                    list[i].move.to_sub = s;
                    list[i].placed = true;
                    list.push_back(std::move(rest)); // may reallocate: nothing below holds a reference
                    found = true;
                }
            }
        }
        if (consents && !found) {
            // FUN_0056f5c0: the first empty slot no entry has taken.
            for (int s = 0; s < bag.bag_slots; ++s) {
                if (left_in(to_cursor, s) || taken(to_cursor, s)) continue;
                list[i].move.to_sub = s;
                list[i].placed = true;
                found = true;
                break;
            }
        }
        if (!found) {
            // CheckBag: 0x20 when the bag refuses the item, else 7; MoveItem's messages.
            if (!consents) {
                if (sources == 1) {
                    plan.eqstr = 12934;
                    plan.args = {cursor.name, target.name};
                } else {
                    plan.eqstr = 5215;
                }
            } else {
                plan.eqstr = 5214;
            }
            return plan;
        }
    }
    for (const Entry& e : list) plan.moves.push_back(e.move);
    return plan;
}

} // namespace nmsc::game
