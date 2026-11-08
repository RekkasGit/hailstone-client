#pragma once

// Developer group and pet windows drawn with Dear ImGui until the stock UI
// engine (M5): the group window (members with HP and mana bars, the leader
// marked, click a member to target it; Invite/Follow, Disband, Decline as the
// stock GroupWindow's buttons), the invitation prompt, and the pet window
// (the focused pet's name, HP, target and buffs, the command buttons, and on
// NMS servers the list of all pets to pick the focused one). Like
// ui/dev_screens.h they draw from a view model and return what the player
// asked for; none of them does networking.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hailstone::ui {

struct GroupMemberView {
    std::string name;
    int level = 0;           // 0 = unknown (not in this zone)
    int hp_percent = -1;     // -1 = not in this zone
    int mana_percent = -1;   // -1 = unknown
    int endurance_percent = -1;
    bool leader = false;
    bool self = false;
    bool targeted = false;
    bool offline = false;
    std::uint32_t spawn_id = 0; // 0 = not in this zone
};

struct GroupWindowView {
    bool in_group = false;
    std::vector<GroupMemberView> members; // in slot order, the player included
    std::string inviter;                  // a pending invitation
    bool open = true;
};

struct PetButtonView {
    const char* label;
    std::uint32_t command;
    bool toggled = false; // shown pressed (OP_PetCommandState)
};

struct PetListRowView {
    std::uint32_t spawn_id = 0;
    std::string name;  // "" when the spawn is not here
    std::string klass; // summoning class
    int hp_percent = -1;
    bool focused = false;
};

struct PetBuffView {
    std::string name;
    std::string time_left;
};

struct PetWindowView {
    bool has_pet = false;
    std::uint32_t pet_id = 0;
    std::string name;
    int level = 0;
    int hp_percent = 0;
    std::string target;   // the pet's target ("" none)
    int target_hp = -1;
    std::vector<PetBuffView> buffs;
    std::vector<PetButtonView> buttons;
    std::vector<PetListRowView> pets; // NMS: every pet; shown when more than one
    bool open = true;
};

struct GroupActions {
    std::optional<std::uint32_t> target_spawn; // a member row clicked
    bool invite = false;                       // Invite / Follow
    bool disband = false;
    bool decline = false;
    bool accept = false;                       // the prompt's Follow
    std::optional<std::uint32_t> pet_command;  // a pet button
    std::optional<std::uint32_t> focus_pet;    // a pet list row clicked
};

void group_window(GroupWindowView& view, GroupActions& actions);
// The "X invites you to join a group." prompt; drawn while view.inviter is set.
void invite_prompt(const GroupWindowView& view, GroupActions& actions);
void pet_window(PetWindowView& view, GroupActions& actions);

} // namespace hailstone::ui
