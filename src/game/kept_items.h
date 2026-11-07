#pragma once

// Hailstone: the items a player marked Keep in the Advanced Inventory window (ui/<pack>/inventorymanager.lua), by
// item id: every copy of a kept item is kept. Not in the original client. While an item is kept, the client refuses
// to destroy it, drop it on the ground, put it in a trade or a give (player or NPC), sell it to a merchant or send it
// by parcel (net::GameConnection's guards; docs/mechanics.md "Kept items"); a bag holding a kept item counts as
// kept for those. The marks are the player's alone: nothing goes to the server.
//
// Kept per character in kept_items.txt in the character's folder (core/character_files.h): one item a line,
// "<id> <name>" (the name only for a person reading the file), lines starting with '#' ignored.

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace nmsc::net {
struct Item;
}

namespace nmsc::game {

class KeptItems {
public:
    bool kept(std::uint32_t id) const { return id != 0 && items_.count(id) != 0; }
    // The item kept, or a bag holding one.
    bool holds_kept(const net::Item& item) const;
    // Marks or unmarks `id` (0 is never kept); `name` is what the file shows for it. True when it changed.
    bool set(std::uint32_t id, std::string_view name, bool on);
    void clear() { items_.clear(); }
    bool empty() const { return items_.empty(); }
    const std::map<std::uint32_t, std::string>& items() const { return items_; }

    // The file's text: lines "<id>[ <name>]"; a line without a number first, or with id 0, is skipped.
    void parse(std::string_view text);
    std::string format() const;

private:
    std::map<std::uint32_t, std::string> items_; // id -> name
};

} // namespace nmsc::game
