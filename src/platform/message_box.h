#pragma once

// A message box for an error that stops Hailstone before or outside its window (a player who starts it by
// double-clicking sees no console). Logged too by the caller.

#include <string>

namespace nmsc::platform {

// Shows `text` under `title` and waits for OK. Does nothing when the system has no way to show one.
void show_error_box(const std::string& title, const std::string& text);

} // namespace nmsc::platform
