#pragma once

// Glue between net::GameConnection's social state (game/social.h) and the
// guild, raid and friends windows (ui/social_windows.h): builds the view
// models each frame, draws them and turns the player's clicks into the same
// slash commands the original's windows run.

#include <chrono>

#include "ui/social_windows.h"

namespace hailstone::net { class GameConnection; }

namespace hailstone::app {

class SocialUi {
public:
    void draw(net::GameConnection& conn);
    void toggle_guild() { guild_.open = !guild_.open; }
    void toggle_raid() { raid_.open = !raid_.open; }
    void toggle_friends() { friends_.open = !friends_.open; }
    // Testing (--auto-accept): accept raid and guild invitations after 2 s.
    void set_testing(bool accept_invites) { auto_accept_ = accept_invites; }

private:
    ui::GuildWindowView guild_;
    ui::RaidWindowView raid_;
    ui::FriendsWindowView friends_;
    bool auto_accept_ = false;
    bool raid_invited_ = false;
    std::chrono::steady_clock::time_point raid_invite_seen_{}, guild_invite_seen_{};
};

} // namespace hailstone::app
