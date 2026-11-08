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
    // FUN_005151b0's table (rof2decomp re/src/game/core/lookup_005151b0.inc).
    static constexpr std::uint32_t k1to16[] = {0x5de, 0x5e2, 0x5e6, 0x5ee, 0x5ea, 0x5f2, 0x5f6, 0x5fa,
                                               0x5fe, 0x602, 0x606, 0x60a, 0x60e, 0x612, 0x616, 0x169f};
    static constexpr std::uint32_t k20to35[] = {0x5cf, 0x5d0, 0x5d1, 0x5d3, 0x5d2, 0x5d4, 0x5d5, 0x5d6,
                                                0x5d7, 0x5d8, 0x5d9, 0x5da, 0x5db, 0x5dc, 0x5dd, 0x16a0};
    if (c >= 1 && c <= 16) return k1to16[c - 1];
    if (c >= 20 && c <= 35) return k20to35[c - 20];
    if (c == 40) return 0x5ce;
    if (c == 41) return 0x5cd;
    if (c == 66) return 0x17de;
    return 0x998;
}

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
    if (s.type == 0) {
        kind = 0x1a88;
        what = s.is_local_player ? 0x1a8b : s.eb9 == 0 ? 0x1a89 : s.eb9 == 1 ? 0x1a8a : 0x1a8d;
        show = true;
    } else if (s.virtual_58) {
        named = false;
        kind = 0x180c;
        what = 0x1a7e;
        show = true;
    } else {
        std::uint8_t c = s.class_id;
        if (c == 0x28) {
            kind = 0x5ce, what = 0x1a83, show = true;
        } else if (c == 0x29) {
            kind = 0x5cd, what = 0x1a82, show = true;
        } else if (c == 0x3c) {
            kind = 0x253, what = 0x1a7f, show = true;
        } else if (c == 0x3d) {
            kind = 0x1d7f, what = 0x1a80, show = true;
        } else if (c >= 0x14 && c <= 0x22 &&
                   (calls.push_back({F::IsGuildmasterForMyClass}), s.guildmaster_for_my_class)) {
            calls.push_back({F::GuildmasterString, c});
            kind = guildmaster_string;
            what = 0x1a84;
            show = kind != 0;
        } else if (s.type >= 2 && s.race < 0x64) {
            kind = 0x1a6f, what = 0x1a7d, show = true;
        } else if (s.race < kRaceAlwaysClickTargetable && targetable()) {
            kind = 0x1a70;
            what = s.eb9 == 0 ? 0x1a8e : s.eb9 == 1 ? 0x1a8f : 0x1a85;
            show = true;
        } else {
            show = kind != 0 && what != 0;
        }
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
    switch (type) {
    case 115: case 116: case 130: case 135: return 4;
    case 120: case 125: return 30;
    case 140: case 145: case 150: case 151: return 10;
    case 146: case 152: return 50;
    default: return 0;
    }
}

} // namespace hailstone::game
