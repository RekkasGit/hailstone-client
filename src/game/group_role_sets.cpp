#include "game/group_role_sets.h"

#include <algorithm>
#include <cctype>
#include <format>

#include "core/ini_file.h"

namespace nmsc::game {

namespace {

constexpr std::string_view kSection = "GroupRoleLoadouts";

int compare_no_case(std::string_view a, std::string_view b) { // __stricmp
    std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        int ca = std::tolower(static_cast<unsigned char>(a[i])), cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

} // namespace

const char* group_role_name(std::int32_t role) {
    switch (role) {
    case 1: return "Main Tank";
    case 2: return "Main Assist";
    case 3: return "Puller";
    default: return nullptr;
    }
}

std::string format_group_roles(std::uint32_t roles, bool numbered) {
    std::string out;
    bool any = false;
    for (std::int32_t role = 1; role < 4; ++role) {
        if (!(roles & group_role_bit(role))) continue;
        const char* separator = any ? ", " : "";
        out += numbered ? std::format("{}({}) {}", separator, role, group_role_name(role))
                        : std::format("{}{}", separator, group_role_name(role));
        any = true;
    }
    if (!any) out += "None";
    return out;
}

void GroupRoleSets::load(const IniFile& ini) {
    for (int n = 0; n < kGroupRoleSets; ++n) {
        GroupRoleSet& s = sets_[static_cast<std::size_t>(n)];
        s = {};
        s.in_use = ini.get_int(kSection, std::format("GroupRoleLoadout{}.inuse", n + 1), 0) != 0;
        if (!s.in_use) continue;
        s.name = ini.get_string(kSection, std::format("GroupRoleLoadout{}.name", n + 1), "Unnamed");
        if (s.name.size() > kGroupRoleSetNameMax - 1) s.name.resize(kGroupRoleSetNameMax - 1);
        for (int k = 0; k < 6; ++k) {
            auto& m = s.members[static_cast<std::size_t>(k)];
            m.name = ini.get_string(kSection, std::format("GroupRoleLoadout{}.name{}", n + 1, k + 1), "Unnamed");
            if (m.name.size() > kGroupRoleSetNameMax - 1) m.name.resize(kGroupRoleSetNameMax - 1);
            m.roles = static_cast<std::uint32_t>(ini.get_int(kSection, std::format("GroupRoleLoadout{}.roles{}", n + 1, k + 1), 0));
        }
    }
    dirty_ = false;
}

void GroupRoleSets::save(IniFile& ini) {
    for (int n = 0; n < kGroupRoleSets; ++n) {
        GroupRoleSet& s = sets_[static_cast<std::size_t>(n)];
        if (!s.changed) continue;
        s.changed = false;
        ini.set_int(kSection, std::format("GroupRoleLoadout{}.inuse", n + 1), s.in_use ? 1 : 0);
        if (!s.in_use) continue;
        ini.set(kSection, std::format("GroupRoleLoadout{}.name", n + 1), s.name);
        for (int k = 0; k < 6; ++k) {
            const auto& m = s.members[static_cast<std::size_t>(k)];
            ini.set(kSection, std::format("GroupRoleLoadout{}.name{}", n + 1, k + 1), m.name);
            ini.set_int(kSection, std::format("GroupRoleLoadout{}.roles{}", n + 1, k + 1), static_cast<int>(m.roles));
        }
    }
}

void GroupRoleSets::sort() {
    std::stable_sort(sets_.begin(), sets_.end(), [](const GroupRoleSet& a, const GroupRoleSet& b) {
        if (a.in_use && b.in_use) return compare_no_case(a.name, b.name) < 0;
        return a.in_use && !b.in_use;
    });
    for (auto& s : sets_) s.changed = true;
    dirty_ = true;
}

int GroupRoleSets::find(std::string_view name) const {
    for (int n = 0; n < kGroupRoleSets; ++n) {
        const GroupRoleSet& s = sets_[static_cast<std::size_t>(n)];
        if (s.in_use && compare_no_case(s.name, name) == 0) return n;
    }
    return -1;
}

int GroupRoleSets::free_slot() const {
    for (int n = 0; n < kGroupRoleSets; ++n)
        if (!sets_[static_cast<std::size_t>(n)].in_use) return n;
    return -1;
}

} // namespace nmsc::game
