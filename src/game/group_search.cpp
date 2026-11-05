#include "game/group_search.h"

#include <algorithm>
#include <cstdlib>
#include <format>

#include "core/ini_file.h"

namespace nmsc::game {

namespace {

constexpr std::string_view kSection = "ADDITIONALFILTERS";
// ClassSortOrder_GetClassId's table.
constexpr std::array<std::uint32_t, 16> kClassOrder{8, 15, 16, 2, 6, 14, 13, 7, 11, 3, 4, 9, 5, 10, 1, 12};
constexpr std::size_t kIniValueMax = 0x40; // IniGetPrivateProfileString's buffer

void load_list(const IniFile& ini, std::string_view prefix, std::vector<std::string>& out) {
    out.clear();
    for (int n = 0;; ++n) {
        std::string v = ini.get_string(kSection, std::format("{}{}", prefix, n), "");
        if (v.size() > kIniValueMax - 1) v.resize(kIniValueMax - 1);
        if (v.empty()) break;
        out.push_back(std::move(v));
    }
}

void save_list(IniFile& ini, std::string_view prefix, const std::vector<std::string>& list) {
    // n = 0 .. count: the names, then one empty key that ends the list for the loader.
    for (std::size_t n = 0; n <= list.size(); ++n)
        ini.set(kSection, std::format("{}{}", prefix, n), n < list.size() ? std::string_view(list[n]) : std::string_view());
}

} // namespace

std::uint32_t group_search_class_id(int index) {
    return index >= 0 && index < 16 ? kClassOrder[static_cast<std::size_t>(index)] : 0;
}

std::uint32_t group_search_class_mask(const std::array<bool, 16>& checked) {
    std::uint32_t mask = 0;
    for (int i = 0; i < 16; ++i)
        if (checked[static_cast<std::size_t>(i)]) mask |= 1u << group_search_class_id(i);
    return mask;
}

std::uint32_t group_search_all_classes() {
    std::uint32_t mask = 0;
    for (int i = 0; i < 16; ++i) mask |= 1u << group_search_class_id(i);
    return mask;
}

std::array<int, 2> group_search_default_levels(int level, bool have_player) {
    int from = have_player ? level - 5 : 1, to = have_player ? level + 5 : 100;
    return {std::max(from, 1), std::min(to, 100)}; // SetIfGreater / SetIfLess
}

bool group_search_range_ok(int from, int to) { return to >= 1 && from <= 100 && from <= to; }

std::uint32_t group_search_error_eqstr(int code) {
    switch (code) {
    case 1: return 6713; // The level range you specified is invalid. ...
    case 2: return 6714; // You must select at least one class to search for. ...
    case 3: return 6715; // You are not the group leader, so you cannot post your group.
    default: return 0;
    }
}

int group_search_int(std::string_view text) { return std::atoi(std::string(text).c_str()); }

int group_search_query_lock(std::uint32_t now_ms, std::uint32_t last_ms) {
    std::uint32_t s = (now_ms - last_ms) / 1000;
    return s < static_cast<std::uint32_t>(kGroupSearchQueryLockSeconds) ? kGroupSearchQueryLockSeconds - static_cast<int>(s) : 0;
}

void GroupSearchFilters::load(const IniFile& ini) {
    player_mode = ini.get_int(kSection, "FilterByPlayerMode", 1);
    guild_mode = ini.get_int(kSection, "FilterByGuildMode", 0);
    exclusive = ini.get_int(kSection, "FilterByExclusiveSearch", 0) == 1;
    load_list(ini, "GUILDEXCLUDED", excluded);
    load_list(ini, "GUILDDESIRED", desired);
}

void GroupSearchFilters::save(IniFile& ini) const {
    ini.set_int(kSection, "FilterByPlayerMode", player_mode);
    ini.set_int(kSection, "FilterByGuildMode", guild_mode);
    ini.set_int(kSection, "FilterByExclusiveSearch", exclusive ? 1 : 0);
    save_list(ini, "GUILDEXCLUDED", excluded);
    save_list(ini, "GUILDDESIRED", desired);
}

void GroupSearchFilters::add_guild(std::vector<std::string>& list, const std::string& name) {
    if (std::find(list.begin(), list.end(), name) == list.end()) list.push_back(name);
}

bool group_search_filtered(const GroupSearchFilters& f, const std::string& name, const std::string& guild,
                           const std::function<bool(const std::string&)>& ignored,
                           const std::function<bool(const std::string&)>& is_friend) {
    bool out = false;
    if (f.player_mode == 1) out = ignored(name);
    if (f.player_mode == 2) out = !is_friend(name);
    if (f.guild_mode == 1) out = std::find(f.excluded.begin(), f.excluded.end(), guild) != f.excluded.end();
    if (f.guild_mode == 2) out = std::find(f.desired.begin(), f.desired.end(), guild) == f.desired.end();
    return out;
}

} // namespace nmsc::game
