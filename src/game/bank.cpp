#include "game/bank.h"

#include "game/merchant.h"

namespace nmsc::game {

using net::Item;

int bank_slot_count(std::uint32_t expansions, const Inventory& inv) {
    if (expansions & 0x400u) return kBankSlotsBig;
    // CDisplay's "hasItems" loop: bank slots 16..23 (ItemBaseContainer::GetItem 0x10..0x17).
    for (std::int16_t s = kBankSlotsSmall; s < kBankSlotsBig; ++s)
        if (inv.at({net::kInvBank, s, -1, -1, 0, 0})) return kBankSlotsBig;
    return kBankSlotsSmall;
}

std::uint32_t* BankCoins::of(int type, bool shared) {
    if (shared) return type == 3 ? &shared_platinum : nullptr;
    switch (type) {
    case 0: return &copper;
    case 1: return &silver;
    case 2: return &gold;
    case 3: return &platinum;
    default: return nullptr;
    }
}

// ---------------------------------------------------------------- rules

namespace {

bool recurses(const Item& item) { return item.item_class == 0 || item.item_class == 1; }

// The message for a refusal found by a recursive test: the item itself, a
// bag's content, or (for a common item) an augment.
std::uint32_t pick(bool itself, const Item& item, std::uint32_t own, std::uint32_t in_bag, std::uint32_t in_aug) {
    if (itself) return own;
    return item.is_bag() ? in_bag : in_aug;
}

bool lore_in_shared_bank(const Item& item, const Inventory& inv, bool contents) {
    if (contents)
        for (const auto& b : item.contents)
            if (lore_in_shared_bank(b.item, inv, true)) return true;
    if (!item.is_lore()) return false;
    for (const auto& [key, there] : inv.items()) {
        if (key.first != net::kInvSharedBank) continue;
        if (shares_lore(item, there)) return true;
        if (there.is_bag())
            for (const auto& b : there.contents)
                if (shares_lore(item, b.item)) return true;
    }
    return false;
}

bool owns_lore(const Item& item, const Item& there) {
    if (shares_lore(item, there)) return true;
    for (const auto& b : there.contents)
        if (owns_lore(item, b.item)) return true;
    return false;
}

bool has_lore_item(const Item& item, const Inventory& inv, bool contents) {
    if (contents)
        for (const auto& b : item.contents)
            if (has_lore_item(b.item, inv, true)) return true;
    if (!item.is_lore()) return false;
    for (const auto& [key, there] : inv.items()) {
        // PcClient_OwnsLoreItemDefinition (rof2decomp batch 254): every profile's possessions with their bag
        // contents, the bank, limbo, alt storage, mail (the last flag) and real estate; not the shared bank or
        // the archived items.
        if (key.first != net::kInvPossessions && key.first != net::kInvBank && key.first != net::kInvLimbo &&
            key.first != net::kInvAltStorage && key.first != net::kInvMail && key.first != net::kInvRealEstate)
            continue;
        if (owns_lore(item, there)) return true;
    }
    return false;
}

} // namespace

bool is_no_storage(const Item& item, bool contents) {
    if (item.no_transfer) return true;
    if (!recurses(item) || !contents) return false;
    for (const auto& b : item.contents)
        if (is_no_storage(b.item, true)) return true;
    return false;
}

bool is_permanent(const Item& item, bool contents) {
    bool permanent = item.no_rent != 0;
    if (!recurses(item) || !permanent || !contents) return permanent;
    for (const auto& b : item.contents)
        if (!is_permanent(b.item, true)) return false;
    return true;
}

bool is_no_trade_non_heirloom(const Item& item, bool contents) {
    if (!item.heirloom && !is_droppable(item)) return true;
    if (!contents) return false;
    for (const auto& b : item.contents)
        if (is_no_trade_non_heirloom(b.item, true)) return true;
    return false;
}

bool shares_lore(const Item& a, const Item& b) {
    if (a.lore_group > 0) return a.lore_group == b.lore_group;
    if (a.lore_group < 0) return a.id == b.id;
    return false;
}

Refusal bank_refusal(const Item& item) {
    if (!is_no_storage(item, true)) return {};
    return {pick(is_no_storage(item, false), item, 6191, 5301, 5302), {}};
}

Refusal shared_bank_refusal(const Item& item, const Inventory& inv) {
    if (lore_in_shared_bank(item, inv, true)) return {pick(lore_in_shared_bank(item, inv, false), item, 3194, 5307, 5308), {}};
    if (is_no_trade_non_heirloom(item, true))
        return {pick(is_no_trade_non_heirloom(item, false), item, 3195, 5303, 5304), "No Trade and non-Heirloom"};
    if (!is_permanent(item, true)) return {pick(!is_permanent(item, false), item, 3195, 5303, 5304), "Temporary"};
    if (is_no_storage(item, true)) return {pick(is_no_storage(item, false), item, 3195, 5303, 5304), "No Storage"};
    return {};
}

Refusal lore_pickup_refusal(const Item& item, const Inventory& inv) {
    if (!has_lore_item(item, inv, true)) return {};
    return {pick(has_lore_item(item, inv, false), item, 379, 5305, 5306), {}};
}

} // namespace nmsc::game
