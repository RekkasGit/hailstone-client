#include "game/clicks.h"

namespace hailstone::game {

std::uint32_t spawn_pick_mask(const MaskSpawn& s, std::uint8_t option) {
    std::uint32_t flags = s.type == 1 ? 6u : 2u;
    if (s.click_through) flags |= kPickMaskClickThrough;
    if (s.mercenary && (option & 4) != 0) {
        flags |= kPickMaskClickThrough;
    } else if (s.type == 1 && s.owner_is_player) {
        if ((option & 2) != 0) flags |= kPickMaskClickThrough;
        else flags &= ~kPickMaskClickThrough;
    }
    return flags;
}

PickCall pick_call(const PickCallInput& in) {
    PickCall out;
    if (in.world_state != 1) return out;
    out.call_dll = true;
    if (in.camera_type == 0 || (in.click_through & 1) != 0) {
        out.exclude_view = in.view_actor_nonnull;
        if (in.have_local_player && in.view_actor_is_local) {
            out.mount_lookups = 1;
            if (in.have_mount) {
                out.mount_lookups = in.mount_has_actor ? 3 : 2;
                out.exclude_mount = in.mount_has_actor;
            }
        }
    }
    out.x = static_cast<std::int32_t>(static_cast<std::uint32_t>(in.x) - static_cast<std::uint32_t>(in.screen_left));
    out.y = static_cast<std::int32_t>(static_cast<std::uint32_t>(in.y) - static_cast<std::uint32_t>(in.screen_top));
    return out;
}

std::uint32_t guildmaster_string_id(std::int32_t c) {
    // 0x5151b0's table: classes 1..15 take every fourth id from 0x5de, their guildmasters 20..34 every id from
    // 0x5cf, both with the fourth and the fifth swapped; the Berserker (16) and its guildmaster (35) came later.
    auto swap_fourth_fifth = [](std::int32_t i) { return i == 3 ? 4 : i == 4 ? 3 : i; };
    if (c >= 1 && c <= 15) return 0x5de + 4 * static_cast<std::uint32_t>(swap_fourth_fifth(c - 1));
    if (c == 16) return 0x169f;
    if (c >= 20 && c <= 34) return 0x5cf + static_cast<std::uint32_t>(swap_fourth_fifth(c - 20));
    if (c == 35) return 0x16a0;
    if (c == 40) return 0x5ce;
    if (c == 41) return 0x5cd;
    if (c == 66) return 0x17de;
    return 0x998;
}

namespace {

// The click lines' description by the spawn's +0xeb9 (0, 1 or anything else).
std::uint32_t by_eb9(std::uint8_t eb9, std::uint32_t zero, std::uint32_t one, std::uint32_t other) {
    if (eb9 == 0) return zero;
    return eb9 == 1 ? one : other;
}

// The classes whose click lines are fixed (the bankers, the merchants and the like): the kind and the description.
struct ClassClickLines {
    std::uint8_t class_id;
    std::uint32_t kind;
    std::uint32_t what;
};
constexpr ClassClickLines kClassClickLines[] = {
    {0x28, 0x5ce, 0x1a83},
    {0x29, 0x5cd, 0x1a82},
    {0x3c, 0x253, 0x1a7f},
    {0x3d, 0x1d7f, 0x1a80},
};

const ClassClickLines* fixed_class_lines(std::uint8_t c) {
    for (const ClassClickLines& row : kClassClickLines)
        if (row.class_id == c) return &row;
    return nullptr;
}

} // namespace

LeftClickResult left_clicked_on_player(const LeftClickInput& in, std::uint32_t guildmaster_string) {
    using F = LeftClickCall::Fn;
    LeftClickResult out;
    if (in.character_list_open) return out;
    out.handled = true;
    auto& calls = out.calls;
    if (in.has_old_target) {
        calls.push_back({F::TargetManagerGet});
        out.save_old_target = true;
    }
    const LeftClickSpawn& s = in.spawn;
    auto targetable = [&] {
        calls.push_back({F::IsTargetable});
        return s.targetable;
    };
    out.target_is_clicked = true;
    if (s.race < kRaceAlwaysClickTargetable && !targetable()) out.target_is_clicked = in.old_target_is_clicked;
    std::uint32_t kind = 0, what = 0;
    if (out.target_is_clicked && s.class_id == 0x3e) {
        calls.push_back({F::TipActivate, 0x22, 0});
        out.show_tip = true;
        kind = 0x1a86;
        what = 0x1a81;
    }
    bool show = false;
    bool named = true;
    auto my_guildmaster = [&](std::uint8_t c) {
        if (c < 0x14 || c > 0x22) return false;
        calls.push_back({F::IsGuildmasterForMyClass});
        return s.guildmaster_for_my_class;
    };
    auto set_lines = [&](std::uint32_t k, std::uint32_t w) {
        kind = k;
        what = w;
        show = true;
    };
    const ClassClickLines* fixed = s.type == 0 || s.virtual_58 ? nullptr : fixed_class_lines(s.class_id);
    if (s.type == 0) {
        set_lines(0x1a88, s.is_local_player ? 0x1a8b : by_eb9(s.eb9, 0x1a89, 0x1a8a, 0x1a8d));
    } else if (s.virtual_58) {
        named = false;
        set_lines(0x180c, 0x1a7e);
    } else if (fixed) {
        set_lines(fixed->kind, fixed->what);
    } else if (my_guildmaster(s.class_id)) {
        calls.push_back({F::GuildmasterString, s.class_id});
        kind = guildmaster_string;
        what = 0x1a84;
        show = kind != 0;
    } else if (s.type >= 2 && s.race < 0x64) {
        set_lines(0x1a6f, 0x1a7d);
    } else if (s.race < kRaceAlwaysClickTargetable && targetable()) {
        set_lines(0x1a70, by_eb9(s.eb9, 0x1a8e, 0x1a8f, 0x1a85));
    } else {
        show = kind != 0 && what != 0;
    }
    if (show && in.click_lines) {
        out.lines = true;
        out.named = named;
        out.kind = kind;
        out.what = what;
        if (named) calls.push_back({F::TrimName});
        calls.push_back({F::GetString, static_cast<std::int32_t>(kind), 0});
        calls.push_back({F::BuildString, named ? 0x1a7c : 0x1a87});
        calls.push_back({F::GetString, static_cast<std::int32_t>(kind), 0});
        calls.push_back({F::BuildString, static_cast<std::int32_t>(what)});
        calls.push_back({F::DspChat, 15, 1});
        calls.push_back({F::DspChat, 15, 1});
    }
    if (in.cursor_holds_something) {
        calls.push_back({F::DropItemOrMoney});
        out.drop = true;
    }
    return out;
}

std::uint32_t switch_damage(std::uint8_t type) {
    // The trap types that hurt, by type; every other switch does no damage.
    struct TrapDamage {
        std::uint8_t type;
        std::uint32_t damage;
    };
    static constexpr TrapDamage kTraps[] = {
        {115, 4},  {116, 4},  {120, 30}, {125, 30}, {130, 4},  {135, 4},
        {140, 10}, {145, 10}, {146, 50}, {150, 10}, {151, 10}, {152, 50},
    };
    for (const TrapDamage& trap : kTraps)
        if (trap.type == type) return trap.damage;
    return 0;
}

} // namespace hailstone::game
