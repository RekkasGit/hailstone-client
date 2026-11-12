#include "game/item_preview.h"

#include <cctype>
#include <charconv>

#include "game/appearance.h"
#include "game/mechanics/item_stats.h"

namespace hailstone::game {

namespace {

bool two_handed(const net::Item& i) { return i.item_type == 1 || i.item_type == 4 || i.item_type == 35; }

std::uint32_t held_model(const net::Item* i) { return i ? item_model_number(i->id_file) : 0; }

} // namespace

bool worn_in_visible_slot(const net::Item& item) {
    const std::uint32_t slots = mechanics::item_equip_slots(item, true);
    return (slots & 0xe7c00u) != 0 || (slots & 0x84u) != 0;
}

int preview_material_slot(int s) {
    // The inventory slot each material slot 0..8 shows (head, chest, arms, wrist, hands, legs, feet, primary,
    // secondary); 0x4dcbe0 answers the other way round, -1 for a slot without one.
    static constexpr int kInventorySlotOfMaterial[] = {2, 17, 7, 10, 12, 18, 19, 13, 14};
    int material = 0;
    for (int slot : kInventorySlotOfMaterial) {
        if (slot == s) return material;
        ++material;
    }
    return -1;
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
    auto held = [&](int slot, const net::Item* shown) {
        apply_wear_change(a, static_cast<std::uint8_t>(slot), held_model(shown), shown ? shown->heros_forge_model : 0,
                          shown ? shown->color : 0, shown ? shown->material_unknown1 : 0,
                          shown ? shown->elite_material : 0, shown ? shown->material_unknown2 : 0);
    };
    if (material == kSlotPrimary) {
        held(kSlotSecondary, two_handed(item) ? nullptr : secondary);
        held(kSlotPrimary, &item);
    } else if (material == kSlotSecondary) {
        held(kSlotSecondary, &item);
        held(kSlotPrimary, primary && !two_handed(*primary) ? primary : nullptr);
    } else if (material >= 0) {
        apply_wear_change(a, static_cast<std::uint8_t>(material), item.material, item.heros_forge_model, item.color,
                          item.material_unknown1, item.elite_material, item.material_unknown2);
    } else if (slots & 0x800u) {
        held(kSlotPrimary, nullptr);
        held(kSlotSecondary, &item);
    }
    return a;
}

} // namespace hailstone::game
