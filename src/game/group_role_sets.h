#pragma once

// Group roles as text and /grouproles' named role sets (rof2decomp re/src/game/world/group_roles.cpp,
// re/src/game/commands/grouproles_command.cpp).
//  - GroupRole_GetName @0x7d5d20: 1 "Main Tank", 2 "Main Assist", 3 "Puller", else none.
//  - GroupRoles_FormatList @0x7d5f90: a member's roles, ", "-separated, numbered "<role>(<n>) <name>" or plain;
//    "None" when it has none.
//  - The role sets (0xe5db44, ten 0x334-byte slots: six member records {u32 roles, char name[]}, the set's name, in
//    use +0x19, changed +0x1a), kept in the character ini (here hotbuttons.ini) section [GroupRoleLoadouts] as
//    SaveGroupRoleLoadoutsToIni @0x546280 writes them: for each changed set GroupRoleLoadout<n>.inuse and, in use,
//    .name, .name1 .. .name6 and .roles1 .. .roles6 (n from 1). Two bugs of the original are fixed here (a change):
//    the saver writes each member's name with "%d" (the pointer's value), and ReadCharacterIniSettings reads the
//    members from a section [GroupRoleLoadout] nobody writes, so a set came back with "Unnamed" members and no roles.
//    Here the names are written as text and read back from [GroupRoleLoadouts].
//  - Cmd_grouproles_SortMembers @0x519a20 (qsort of the sets): in use first, by name, as CEverQuest::SortSpellLoadouts.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace nmsc { class IniFile; }

namespace nmsc::game {

inline constexpr int kGroupRoleSets = 10;
inline constexpr std::size_t kGroupRoleSetNameMax = 24; // the loader's 0x18 buffer

// GroupRole_GetName: nullptr for a role that is not 1..3.
const char* group_role_name(std::int32_t role);
// The role bits a member keeps (CGroupMemberBase_SetRoleFlag +0x1c): 1 tank, 2 assist, 4 puller.
inline constexpr std::uint32_t group_role_bit(std::int32_t role) { return role >= 1 && role <= 3 ? 1u << (role - 1) : 0; }
// GroupRoles_FormatList over a member's role bits.
std::string format_group_roles(std::uint32_t roles, bool numbered);

struct GroupRoleSetMember {
    std::uint32_t roles = 0; // the role bits
    std::string name;
};

struct GroupRoleSet {
    std::array<GroupRoleSetMember, 6> members{};
    std::string name;
    bool in_use = false;
    bool changed = false; // written by the next save
};

class GroupRoleSets {
public:
    void load(const IniFile& ini);
    void save(IniFile& ini); // the changed sets; they are no longer changed
    void sort();             // in use first, by name (case ignored); every set changed

    // The group's own index of a set by name (the group's virtual +0xc: the slot whose name matches, case ignored,
    // among those in use); -1 none.
    int find(std::string_view name) const;
    // The first unused slot, -1 when all ten are in use.
    int free_slot() const;
    GroupRoleSet& at(int n) { return sets_[static_cast<std::size_t>(n)]; }
    const GroupRoleSet& at(int n) const { return sets_[static_cast<std::size_t>(n)]; }
    void mark_dirty() { dirty_ = true; }
    bool take_dirty() {
        bool d = dirty_;
        dirty_ = false;
        return d;
    }

private:
    std::array<GroupRoleSet, kGroupRoleSets> sets_{};
    bool dirty_ = false;
};

} // namespace nmsc::game
