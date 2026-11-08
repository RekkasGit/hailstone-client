#include "game/anim_layers.h"

#include <array>

#include "game/mechanics/anim_select.h"

namespace hailstone::game {

namespace mech = mechanics;

namespace {

// The three tables 0x4afb70 fills (byte per old-style number 0..0x4f).
struct OldTables {
    std::array<bool, 0x50> interruptible{}; // 0xdcece8
    std::array<bool, 0x50> over_legs{};     // 0xdced38
    std::array<bool, 0x50> under_action{};  // 0xdced88
};
constexpr OldTables make_tables() {
    OldTables t;
    auto set = [](std::array<bool, 0x50>& a, int from, int to) {
        for (int i = from; i <= to; ++i) a[i] = true;
    };
    set(t.interruptible, 0, 0);
    set(t.interruptible, 0x11, 0x12);
    set(t.interruptible, 0x15, 0x20);
    set(t.interruptible, 0x22, 0x23);
    set(t.interruptible, 0x25, 0x26);
    set(t.interruptible, 0x30, 0x4e);
    set(t.over_legs, 2, 9);
    set(t.over_legs, 0x27, 0x2c);
    set(t.under_action, 0x11, 0x12);
    set(t.under_action, 0x20, 0x20);
    set(t.under_action, 0x4a, 0x4c);
    set(t.under_action, 0x4e, 0x4e);
    return t;
}
constexpr OldTables kTables = make_tables();

bool starts(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

} // namespace

std::uint8_t classic_bone_group(std::string_view tag) {
    if (tag.size() < 3) return kLowerBones;
    std::string_view s = tag.substr(3); // past the model code
    if (starts(s, "CHCHEST_")) return kLowerBones;
    for (std::string_view p : {"FA", "HE", "FI", "FO", "BI", "NE", "HI", "CH"})
        if (starts(s, p)) return kUpperBones;
    return kLowerBones;
}

std::uint8_t new_style_bone_group(std::string_view tag) {
    if (starts(tag, "CHEST")) return tag == "CHEST_CHEST01" ? kLowerBones : kUpperBones;
    if (starts(tag, "HEAD") || starts(tag, "ARM") || starts(tag, "NECK")) return kUpperBones;
    return kLowerBones;
}

bool classic_action_over_legs(std::uint8_t old) { return old < 0x50 && kTables.over_legs[old]; }
bool classic_legs_under_action(std::uint8_t old) { return old < 0x50 && kTables.under_action[old]; }
bool classic_interruptible(std::uint8_t old) { return old < 0x50 && kTables.interruptible[old]; }

bool classic_action_upper_only(std::uint8_t action, bool mounted, bool humanoid, float speed_run) {
    // The rider animation is queued (group 1) and the action is the target (group 0).
    if (mounted) return true;
    // +0x7c not 0, HasProperty(1) and the table at 0xdced38: nothing is queued, group 1 is left alone.
    return speed_run != 0.0f && humanoid && classic_action_over_legs(action);
}

bool classic_legs_take_motion(std::uint8_t playing, std::uint8_t wanted, bool humanoid) {
    if (!humanoid || !classic_legs_under_action(wanted) || !classic_action_over_legs(playing)) return false;
    // P01 and the rider animations also need the playing animation in 0xdced88, which no action is.
    return wanted != mech::kOldStand && wanted != 0x4a && wanted != 0x4e;
}

bool classic_may_interrupt(std::uint8_t requested, std::uint8_t playing, bool class_0x3e) {
    auto blend = [](std::uint8_t a) { return a == 0x1a || a == 0x48 || a == 0x49; };
    if (requested == 0x4a || requested == 0x4e || blend(requested)) return true; // key 1
    // The object's slot 0x38 (0x4b07e0) over a blend animation.
    const bool standing = requested == 0x20 || requested == 0x26 || requested == 0x47 || requested == 0x4a;
    const bool over_blend = standing && blend(playing);
    if (requested == 0xf) {
        if (class_0x3e) return true;
    } else if (requested == 0x10 || requested == 0x21 || requested == 0x2c) {
        return true;
    }
    if (over_blend) return false;
    if (playing == requested) return false;
    if (playing >= 0x4f) return true;
    return kTables.interruptible[playing];
}

bool classic_queues(std::uint8_t anim) { return (anim >= 1 && anim <= 0xb) || (anim >= 0x2d && anim <= 0x2f); }

bool classic_clears_queue(std::uint8_t anim) {
    return (anim >= 0x11 && anim <= 0x19) || anim == 0x21 || anim == 0x24 || anim == 0x4b || anim == 0x4c;
}

bool classic_plain_stand_after(std::uint8_t a) {
    return a == 0x20 || (a >= 1 && a <= 0x10) || (a >= 0x2a && a <= 0x2f) || a == 0xfe || a == 0x11 || a == 0x4b ||
           a == 0x22;
}

std::uint8_t new_style_hold_kind(std::uint8_t code) {
    if (code == 0x92) return 1;
    if ((code >= 0x68 && code <= 0x6c) || (code >= 0x6e && code <= 0x7a) || (code >= 0x7d && code <= 0x81)) return 3;
    return 0;
}

bool new_style_loops(std::uint8_t code) {
    if ((code >= 0x68 && code <= 0x6c) || (code >= 0x7d && code <= 0x81) || code == 0x97) return true;
    return code == 0x6e || code == 0x70 || code == 0x72 || code == 0x74 || code == 0x76;
}

std::uint8_t new_style_layer_mode(const LayerModeInput& in) {
    if (!in.group) return 3;
    std::uint8_t wanted = in.wanted, playing = in.playing;
    if (in.race_model_0x3e) {
        if (playing == 0x91) playing = 0x92;
        else if (wanted == 0x91) wanted = 0x92;
    }
    if (wanted == playing && in.column == in.playing_column) return 3;
    if (wanted == 0x92) return !in.dying_slot_0x98 || in.completed ? 2 : 1;
    if (playing == mech::kNoAnimation || in.forced) return 2;
    std::uint8_t hold = new_style_hold_kind(playing);
    if (playing == 0x92 && in.stand_state != 0x73 && in.spawn_type < 2) hold = 3;
    if (hold == 1) return 0;
    if (in.completed) return 2;
    if (hold != 3) return 1;
    // A looping sequence: one of higher priority keeps playing unless it is a movement one (0x589730).
    auto moving = [](std::uint8_t c) {
        return c == 0x68 || c == 0x69 || c == 0x6a || c == 0x6b || c == 0x6c || c == 0x7d || c == 0x7e || c == 0x80;
    };
    if (mech::new_style_priority(playing) > mech::new_style_priority(wanted) && !moving(playing)) return 1;
    // 0x5883d0: sitting down / standing up / the seated idle are not cut by a stand or a sit.
    if ((playing == 0x77 || playing == 0x78 || playing == 0x79) && (wanted == 0x6e || wanted == 0x76)) return 1;
    return 2;
}

} // namespace hailstone::game
