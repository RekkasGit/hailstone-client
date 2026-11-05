#pragma once

// Developer guild, raid and friends windows drawn with Dear ImGui until the
// stock UI engine (M5): the guild management window (members with level,
// class, rank, zone and note; MOTD; Invite, Remove, Leave), the guild
// invitation prompt, the raid window (members by group, leader and lock;
// Invite, Accept and Decline while invited, Leave) and the friends and
// ignore lists. Like ui/group_windows.h they draw from a view model and
// return what the player asked for; none of them does networking.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nmsc::ui {

struct GuildMemberView {
    std::string name;
    int level = 0;
    std::string klass;
    std::string rank;
    std::string zone;  // "" offline
    std::string note;
};

struct GuildWindowView {
    bool in_guild = false;
    std::string guild_name;
    std::string motd, motd_set_by;
    std::vector<GuildMemberView> members;
    int selected = -1;
    bool show_offline = true;
    std::string motd_edit;
    bool open = false;
    // A pending invitation.
    std::string inviter, invite_guild;
};

struct RaidMemberView {
    std::string name;
    int level = 0;
    std::string klass;
    bool group_leader = false;
    bool leader = false;
};

struct RaidWindowView {
    bool in_raid = false;
    bool locked = false;
    std::string leader, motd;
    std::vector<std::vector<RaidMemberView>> groups; // 12 groups, then the ungrouped
    std::string inviter;
    bool open = false;
};

struct FriendsWindowView {
    std::vector<std::string> friends, ignored;
    bool chat_connected = false;
    std::string edit;
    bool open = false;
};

struct SocialActions {
    bool guild_invite = false;          // invite the target
    std::optional<std::string> guild_remove;
    bool guild_leave = false;
    std::optional<std::string> guild_motd;
    std::optional<bool> guild_answer;   // the invitation prompt: accept / decline
    bool raid_invite = false;           // invite the target
    std::optional<bool> raid_answer;
    bool raid_leave = false;
    std::optional<std::string> add_friend, remove_friend, add_ignore, remove_ignore;
};

void guild_window(GuildWindowView& view, SocialActions& actions);
void guild_invite_prompt(const GuildWindowView& view, SocialActions& actions);
void raid_window(RaidWindowView& view, SocialActions& actions);
void friends_window(FriendsWindowView& view, SocialActions& actions);

} // namespace nmsc::ui
