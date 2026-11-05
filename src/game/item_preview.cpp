#include "game/item_preview.h"

#include <cctype>
#include <charconv>

#include "game/appearance.h"
#include "game/mechanics/item_stats.h"

namespace nmsc::game {

namespace {

bool two_handed(const net::Item& i) { return i.item_type == 1 || i.item_type == 4 || i.item_type == 35; }

std::uint32_t held_model(const net::Item* i) { return i ? item_model_number(i->id_file) : 0; }

} // namespace

bool worn_in_visible_slot(const net::Item& item) {
    const std::uint32_t slots = mechanics::item_equip_slots(item, true);
    return (slots & 0xe7c00u) != 0 || (slots & 0x84u) != 0;
}

int preview_material_slot(int s) {
    switch (s) {
    case 2: return 0;
    case 7: return 2;
    case 10: return 3;
    case 12: return 4;
    case 13: return 7;
    case 14: return 8;
    case 17: return 1;
    case 18: return 5;
    case 19: return 6;
    default: return -1;
    }
}

std::uint32_t item_model_number(std::string_view id) {
    if (id.size() < 3 || std::toupper(static_cast<unsigned char>(id[0])) != 'I' ||
        std::toupper(static_cast<unsigned char>(id[1])) != 'T')
        return 0;
    std::uint32_t n = 0;
    std::from_chars(id.data() + 2, id.data() + id.size(), n);
    return n;
}

ItemPreviewShow item_preview_show(const net::Item& item, std::uint8_t trailing) {
    ItemPreviewShow s;
    const bool visible = worn_in_visible_slot(item);
    const bool modelled = item.id_file != "IT63" && item.id_file != "IT64";
    s.show = visible || modelled || trailing != 0;
    s.on_player = visible || trailing != 0;
    return s;
}

Appearance preview_appearance(Appearance a, const net::Item& item, const net::Item* primary, const net::Item* secondary) {
    const std::uint32_t slots = mechanics::item_equip_slots(item, true);
    int material = -1;
    for (int s = 2; s <= 19 && material < 0; ++s)
        if (slots & (1u << s)) material = preview_material_slot(s);
    const std::uint32_t own = item_model_number(item.id_file);
    if (material == kSlotPrimary) {
        a.material[kSlotSecondary] = two_handed(item) ? 0 : held_model(secondary);
        a.material[kSlotPrimary] = own;
    } else if (material == kSlotSecondary) {
        a.material[kSlotSecondary] = own;
        a.material[kSlotPrimary] = primary && !two_handed(*primary) ? held_model(primary) : 0;
    } else if (material >= 0) {
        apply_wear_change(a, static_cast<std::uint8_t>(material), item.material, item.heros_forge_model, item.color);
    } else if (slots & 0x800u) {
        a.material[kSlotPrimary] = 0;
        a.material[kSlotSecondary] = own;
    }
    return a;
}

} // namespace nmsc::game
