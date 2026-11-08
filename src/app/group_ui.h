#pragma once

// Glue between net::GameConnection's group and pet state and the group and
// pet windows (ui/group_windows.h): builds the view models each frame, draws
// them and passes the player's clicks back to the connection.

#include <chrono>
#include <string>
#include <vector>

#include "ui/group_windows.h"

namespace hailstone::net { class GameConnection; }

namespace hailstone::app {

class GroupUi {
public:
    void draw(net::GameConnection& conn);
    void toggle_group() { group_.open = !group_.open; }
    void toggle_pet() { pet_.open = !pet_.open; }
    // Testing (--auto-commands, --auto-accept): chat lines typed one every 3 s
    // after zoning in, and accepting group invitations.
    void set_testing(const std::string& commands, bool accept_invites);

private:
    ui::GroupWindowView group_;
    ui::PetWindowView pet_;
    std::vector<std::string> auto_commands_;
    std::size_t auto_next_ = 0;
    bool auto_accept_ = false;
    std::chrono::steady_clock::time_point in_zone_since_{}, invite_seen_{}, last_command_{};
};

} // namespace hailstone::app
