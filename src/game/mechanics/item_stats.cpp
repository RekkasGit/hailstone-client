#include "game/mechanics/item_stats.h"

#include "core/wrap_math.h"
#include "game/augments.h"

namespace hailstone::game::mechanics {

namespace {

std::int32_t add(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
// IMUL, then signed / 100.
std::int32_t percent(std::int32_t pct, std::int32_t value) {
    return core::wrapped_div(static_cast<std::uint32_t>(pct) * static_cast<std::uint32_t>(value), 100);
}
bool plain(const net::Item& i) { return i.item_class == 0; }

std::int32_t charm_scaled(const net::Item& item, std::int32_t value) {
    if (item.charm_file_id != 0 && item.scaled_value != 100) {
        auto scale = static_cast<std::int32_t>(item.scaled_value);
        std::uint32_t magnitude = scale < 0 ? 0u - static_cast<std::uint32_t>(scale) : static_cast<std::uint32_t>(scale);
        value = core::wrapped_div(magnitude * static_cast<std::uint32_t>(value), 100);
    }
    return value;
}

// The aug socket's "applies the aug stat percent" byte (+5 of the 8-byte socket entry).
bool socket_scales(const net::Item& owner, int socket) {
    return socket >= 0 && socket <= 5 && owner.aug_sockets[static_cast<std::size_t>(socket)].unknown != 0;
}

} // namespace

std::int32_t aug_stat_percent(const net::Item& item, bool recurse) {
    if (!plain(item)) return 100;
    auto pct = static_cast<std::int32_t>(item.purity);
    if (pct < 0) pct = 0;
    pct = charm_scaled(item, pct);
    if (recurse)
        for (const auto& c : item.contents) {
            std::int32_t child = aug_stat_percent(c.item, recurse);
            if (child > 0) pct = add(pct, child);
        }
    return pct;
}

std::int32_t apply_aug_stat_percent(const net::Item& owner, int socket, std::int32_t value, bool apply) {
    if (apply && socket_scales(owner, socket)) return percent(aug_stat_percent(owner, true), value);
    return value;
}

namespace {

// SumStat in aug mode: a child is summed with ignoreDisabled = its owner's ItemBase_IsStatsDisabled, so a disabled
// child (game::stats_disabled) adds nothing under an enabled owner; the getters themselves pass ignoreDisabled 1.
std::int32_t sum_stat(const net::Item& item, std::int32_t (*field)(const net::Item&), bool recurse,
                      std::int32_t* aug_total, bool ignore_disabled) {
    if (aug_total) *aug_total = 0;
    if (!plain(item)) return 0;
    if (stats_disabled(item) && !ignore_disabled) return 0;
    std::int32_t value = charm_scaled(item, field(item));
    if (!recurse) return value;
    const bool disabled = stats_disabled(item);
    std::int32_t augs = 0;
    for (const auto& c : item.contents) {
        auto socket = static_cast<int>(c.index);
        std::int32_t child_aug = 0;
        std::int32_t child = sum_stat(c.item, field, recurse, &child_aug, disabled);
        if (socket_scales(item, socket)) child = percent(aug_stat_percent(item, true), child);
        value = add(value, child);
        augs = add(augs, child_aug);
        if (socket_scales(item, socket)) augs = add(augs, child);
    }
    if (aug_total) *aug_total = augs;
    return value;
}

} // namespace

std::int32_t item_stat(const net::Item& item, std::int32_t (*field)(const net::Item&), bool recurse) {
    return sum_stat(item, field, recurse, nullptr, true);
}

std::int32_t item_stat(const net::Item& item, std::int32_t (*field)(const net::Item&), std::int32_t& aug_total) {
    return sum_stat(item, field, true, &aug_total, true);
}

std::int32_t item_max_field(const net::Item& item, std::int32_t (*field)(const net::Item&), bool recurse) {
    if (!plain(item)) return 0;
    std::int32_t value = field(item);
    if (recurse)
        for (const auto& c : item.contents) {
            std::int32_t child = item_max_field(c.item, field, recurse);
            if (child > value) value = child;
        }
    return value;
}

std::int32_t item_required_level(const net::Item& item) {
    return item_max_field(item, [](const net::Item& i) { return static_cast<std::int32_t>(i.req_level); });
}
std::int32_t item_recommended_level(const net::Item& item) {
    return item_max_field(item, [](const net::Item& i) { return static_cast<std::int32_t>(i.rec_level); });
}
std::int32_t item_recommended_skill(const net::Item& item) {
    return item_max_field(item, [](const net::Item& i) { return static_cast<std::int32_t>(i.rec_skill); });
}
std::int32_t item_prestige(const net::Item& item) {
    return item_max_field(item, [](const net::Item& i) { return static_cast<std::int32_t>(i.prestige); });
}

std::uint32_t item_races(const net::Item& item) {
    if (!plain(item)) return 0;
    std::uint32_t mask = item.races;
    std::uint32_t children = 0xffffffffu;
    for (const auto& c : item.contents) children &= item_races(c.item);
    return mask & children;
}

std::uint32_t item_classes(const net::Item& item) {
    if (!plain(item)) return 0;
    std::uint32_t children = 0xffffffffu;
    for (const auto& c : item.contents) children &= item_classes(c.item);
    return item.classes & children;
}

std::uint32_t item_equip_slots(const net::Item& item, bool recurse) {
    if (!plain(item)) return 0;
    std::uint32_t slots = item.equip_slots;
    if (recurse)
        for (const auto& c : item.contents) slots &= item_equip_slots(c.item, recurse);
    return slots;
}

std::uint32_t item_deity(const net::Item& item) {
    if (!plain(item)) return 0;
    std::uint32_t deity = item.deity;
    std::uint32_t contents = 0xffffffffu;
    bool any = false;
    for (const auto& c : item.contents) {
        std::uint32_t child = item_deity(c.item);
        if (child != 0) {
            contents &= child;
            any = true;
        }
    }
    if (any) deity = deity == 0 ? contents : deity & contents;
    return deity;
}

std::int32_t item_haste(const net::Item& item, std::int32_t best, bool recurse) {
    if (!plain(item)) return 0;
    std::int32_t haste = charm_scaled(item, item.haste);
    if (haste > best) best = haste;
    if (!recurse) return best;
    for (const auto& c : item.contents) {
        std::int32_t child = item_haste(c.item, 0);
        if (socket_scales(item, static_cast<int>(c.index))) child = percent(aug_stat_percent(item, true), child);
        if (child > best) best = child;
    }
    return best;
}

std::int32_t item_skill_mod(const net::Item& item, std::int32_t skill, std::int32_t base) {
    if (!plain(item)) return 0;
    std::int32_t mod = 0;
    if (item.skill_mod_type == skill) {
        std::int32_t scaled = percent(item.skill_mod_value, base);
        mod = add(static_cast<std::int32_t>(item.skill_mod_extra), scaled); // SkillModBonus (+0x154) + value%
        if (item.skill_mod_max > 0 && mod > item.skill_mod_max) mod = item.skill_mod_max;
    }
    for (const auto& c : item.contents) {
        std::int32_t child = item_skill_mod(c.item, skill, base);
        if (child > mod) mod = child;
    }
    return mod;
}

std::int32_t item_weight(const net::Item& item) {
    // recurse = (definition == null): false for every received item, so no children; checkType 0.
    return charm_scaled(item, item.weight);
}

std::uint32_t item_size(const net::Item& item, bool recurse) {
    std::uint32_t size = item.size;
    if (plain(item) && recurse)
        for (const auto& c : item.contents) {
            std::uint32_t child = item_size(c.item, recurse);
            if (size < child) size = child;
        }
    return size;
}

std::int32_t item_value(const net::Item& item, bool contents) {
    auto value = static_cast<std::int32_t>(item.value);
    if (plain(item) && contents)
        for (const auto& c : item.contents) value = add(value, item_value(c.item, true));
    return value;
}

std::int32_t item_dmg_bonus(const net::Item& item, std::int32_t skill, bool recurse) {
    if (!plain(item)) return 0;
    std::int32_t value = item.extra_dmg_skill == skill ? charm_scaled(item, item.extra_dmg_amt) : 0;
    if (recurse)
        for (const auto& c : item.contents) {
            std::int32_t child = item_dmg_bonus(c.item, skill, recurse);
            if (socket_scales(item, static_cast<int>(c.index))) child = percent(aug_stat_percent(item, true), child);
            value = add(value, child);
        }
    return value;
}

std::int32_t item_potion_slots(const net::Item& item, bool recurse) {
    if (!plain(item)) return 0;
    std::int32_t value = charm_scaled(item, static_cast<std::int32_t>(item.potion_belt_slots));
    if (recurse)
        for (const auto& c : item.contents) {
            std::int32_t child = item_potion_slots(c.item, recurse);
            if (socket_scales(item, static_cast<int>(c.index))) child = percent(aug_stat_percent(item, true), child);
            value = add(value, child);
        }
    return value;
}

std::int32_t item_ac(const net::Item& item, bool recurse) {
    return item_stat(item, [](const net::Item& i) { return i.ac; }, recurse);
}

std::uint8_t item_type_to_skill(std::uint32_t item_type) {
    switch (item_type & 0xff) {
    case 0: return 1;
    case 1: return 3;
    case 2: return 36;
    case 3: return 0;
    case 4: return 2;
    case 5: case 6: case 27: case 28: return 7;
    case 7: case 19: return 51;
    case 9: return 231;
    case 10: return 15;
    case 35: return 77;
    default: return 28;
    }
}

} // namespace hailstone::game::mechanics
