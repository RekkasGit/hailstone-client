#pragma once

// The dialog a player sees when Hailstone stops on an error (app/main.cpp): a double-clicked client has no console,
// so without it the window just never comes, or closes. The error's own text is technical (a Vulkan call, an SDL
// call); this says in plain words what kind of problem it is and what usually fixes it, then gives the error itself
// and the log to look in or attach to a report.

#include <filesystem>
#include <string>
#include <string_view>

namespace nmsc::app {

// The kinds of error a player can do something about; Other for the rest.
enum class StartupProblem {
    NoVulkan,      // no Vulkan loader: no driver installed
    OldVulkan,     // the loader or the driver is older than 1.3
    NoUsableGpu,   // no GPU has what Hailstone needs (render/gpu_requirements.h)
    Window,        // SDL could not start video or make the window or its surface
    MissingFiles,  // Hailstone's own files (shaders) are missing or damaged
    GraphicsError, // another failed Vulkan call
    Other,
};

// Which kind `what` (an exception's text) is.
StartupProblem classify_startup_error(std::string_view what);

// The dialog's text for `what`; `log` is named at the end when it is not empty.
std::string startup_error_text(std::string_view what, const std::filesystem::path& log);

} // namespace nmsc::app
