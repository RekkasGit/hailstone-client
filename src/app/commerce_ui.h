#pragma once

// Glue between net::GameConnection's merchant and trade state and the
// commerce windows (ui/commerce_windows.h): builds the window input each
// frame, draws them and passes the player's clicks back to the connection.
// Drawn by app::ItemUi, which owns the item icons.
//
// Testing aid: NMSC_COMMERCE_TEST in the environment drives the windows
// once in the zone (for screenshots):
//   merchant       open the nearest merchant within reach, select its first
//                  item and the first general-slot item for selling
//   give:ID        pick up the inventory item with item id ID and offer it to
//                  the nearest NPC within reach (the give window stays open)
//   give!:ID       the same, then press Give
//   bank           open the nearest banker within reach, then click the
//                  first general-slot stack (the quantity dialog opens)

#include <chrono>
#include <string>

#include "ui/commerce_windows.h"

namespace nmsc::ui { class ItemIconCache; }
namespace nmsc::net { class GameConnection; }

namespace nmsc::app {

class CommerceUi {
public:
    CommerceUi();
    void draw(net::GameConnection& conn, ui::ItemIconCache* icons);

private:
    void drive_test(net::GameConnection& conn);

    ui::CommerceWindowsState state_;
    std::string test_;
    int test_step_ = 0;
    std::chrono::steady_clock::time_point test_since_{};
};

} // namespace nmsc::app
