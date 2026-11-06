#pragma once

// Leadership abilities (the leadership window). EQEmu's indexes, costs and experience rule (zone/aa.h
// LeadershipAACosts, common/features.h): 0..15 group abilities, 16..31 raid ones, at most 6 ranks, a cost per
// rank (0: no further rank); a group point takes 1000 experience, a raid point 2000. The original's CLeadershipWnd
// table is not decoded: the names are EQEmu's, written out.

#include <array>
#include <cstdint>
#include <string_view>

namespace nmsc::game {

inline constexpr int kLeadershipAbilities = 32, kLeadershipRanks = 6, kFirstRaidAbility = 16;
inline constexpr double kGroupLeadershipExpPerPoint = 1000, kRaidLeadershipExpPerPoint = 2000;

struct LeadershipAbility {
    std::string_view name; // empty: no such ability (EQEmu's placeholders)
    std::array<std::uint8_t, kLeadershipRanks> costs;
};
const LeadershipAbility& leadership_ability(int index);

// The leadership window's experience gauge, 0..1000: `exp` of `per`. The experience is the server's f64, clamped
// before it becomes an int (a NaN or a value past int's range would be undefined); NaN shows as 0.
int leadership_gauge(double exp, double per);

// How many ranks an ability has (its leading non-zero costs) and the cost of the next one (0: none).
int leadership_max_rank(int index);
std::uint32_t leadership_next_cost(int index, std::uint32_t rank);

// The original's leadership ability records (FUN_0055d8e0, rof2decomp re/src/game/core/store_switch_0055d8e0.inc) as
// the leadership window's hot buttons read them: how many hot buttons the ability has (+8: 3 for the marks and
// the main assists, else 1) and the three string ids of its hot button text (+0xc, +0x10, +0x14; 0 none).
struct LeadershipHotButtonDef {
    int count = 1;
    std::array<std::uint32_t, 3> words{};
};
const LeadershipHotButtonDef& leadership_hot_button_def(int ability);
// FUN_00560180's passive abilities (1, 7..12, 17, 22..27), which never get a hot button.
bool leadership_passive(int ability);
// The hot button's value (CLeadershipWnd::WndNotification: ability * 8 + n, the cursor attachment's type 0xd; a hot
// button of type 11 runs Leadership_ActivateAbility(value / 8, value % 8)).
inline constexpr std::int32_t leadership_hot_button_value(int ability, int n) { return ability * 8 + n; }

} // namespace nmsc::game
