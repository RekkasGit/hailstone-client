#pragma once

// At every start: the UI settings of the builds that kept them in the data folder's ui-settings/ are moved to
// platform::ui_settings_dir() (ui/settings/), inside the same Hailstone folder. A file already there is kept and the
// old one left; the old folder goes once empty. Returns the number of files moved.

namespace nmsc {

int move_old_ui_settings();

} // namespace nmsc
