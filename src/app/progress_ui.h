#pragma once

// Temporary ImGui debug windows for the progress systems (AAs, training, tasks) until the Lua UI
// (branch ui-lua) replaces them: the training window and the task select window appear while the
// server has one open; the AA / task / skills panel with NMSC_PROGRESS_WINDOWS=1.
//
// Testing aid: NMSC_PROGRESS_TEST in the environment drives the systems once in the zone:
//   train     open the nearest guildmaster within reach, train the first row the checks allow,
//             then close the window
//   aa        set the AA experience to 50 %, then back to 0, and log the AA table
//   task      hail the nearest NPC within reach and accept the first task offered, then ask for
//             the first completed task's history
//   combine   combine the first bag in the general slots that holds items

#include <chrono>
#include <string>

namespace nmsc::net { class GameConnection; }

namespace nmsc::app {

class ProgressUi {
public:
    ProgressUi();
    void draw(net::GameConnection& conn);

private:
    void drive_test(net::GameConnection& conn);
    void draw_panel(net::GameConnection& conn);

    bool panel_ = false;
    int aa_tab_ = 0;
    std::string test_;
    int test_step_ = 0;
    std::chrono::steady_clock::time_point test_since_{}, step_since_{};
};

} // namespace nmsc::app
