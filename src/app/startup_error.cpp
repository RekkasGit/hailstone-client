#include "app/startup_error.h"

namespace nmsc::app {

namespace {

bool has(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

constexpr std::string_view kUpdateDriver =
    "Install the current driver for your graphics card from its maker (NVIDIA, AMD or Intel). On Linux, install "
    "your distribution's Vulkan driver (Mesa's for AMD and Intel).";

} // namespace

StartupProblem classify_startup_error(std::string_view what) {
    // The texts thrown by render/vk_context.cpp, render/gpu_requirements.cpp and platform/window.cpp.
    if (has(what, "volkInitialize") || has(what, "vkCreateInstance")) return StartupProblem::NoVulkan;
    if (has(what, "Vulkan 1.3 is required")) return StartupProblem::OldVulkan;
    if (has(what, "no usable GPU")) return StartupProblem::NoUsableGpu;
    if (has(what, "SDL_Init") || has(what, "SDL_CreateWindow") || has(what, "SDL_Vulkan")) {
        // With no Vulkan driver installed the window is what fails first, SDL saying why after the call's name
        // ("SDL_CreateWindow failed: Installed Vulkan doesn't implement the VK_KHR_surface extension", seen with the
        // drivers hidden).
        const std::size_t reason = what.find(':');
        if (reason != std::string_view::npos && has(what.substr(reason), "Vulkan")) return StartupProblem::NoVulkan;
        return StartupProblem::Window;
    }
    if (has(what, "cannot open shader") || has(what, "bad SPIR-V")) return StartupProblem::MissingFiles;
    if (has(what, "VkResult") || has(what, "depth format")) return StartupProblem::GraphicsError;
    return StartupProblem::Other;
}

std::string startup_error_text(std::string_view what, const std::filesystem::path& log) {
    std::string headline;
    std::string advice;
    switch (classify_startup_error(what)) {
    case StartupProblem::NoVulkan:
        headline = "Hailstone could not find Vulkan on this computer.";
        advice = "Hailstone draws with Vulkan 1.3, which comes with the graphics driver. ";
        advice += kUpdateDriver;
        break;
    case StartupProblem::OldVulkan:
        headline = "This computer's Vulkan is older than the 1.3 Hailstone needs.";
        advice = "Updating the graphics driver usually brings Vulkan 1.3. ";
        advice += kUpdateDriver;
        break;
    case StartupProblem::NoUsableGpu:
        headline = "No graphics card on this computer can run Hailstone.";
        advice = "Hailstone needs a graphics card whose driver offers Vulkan 1.3 with the features named below. "
                 "Updating the driver is often enough; a card that is too old cannot run it. The README's "
                 "\"System requirements\" lists what is needed. ";
        advice += kUpdateDriver;
        break;
    case StartupProblem::Window:
        headline = "Hailstone could not open its window.";
        advice = "Start Hailstone from the desktop (not over a remote connection without a display), and make sure "
                 "the graphics driver is installed and current.";
        break;
    case StartupProblem::MissingFiles:
        headline = "Some of Hailstone's own files are missing or damaged.";
        advice = "Unpack the whole download again into Hailstone's folder: the shaders folder must be beside the "
                 "program.";
        break;
    case StartupProblem::GraphicsError:
        headline = "The graphics driver reported an error.";
        advice = "Updating the graphics driver often fixes this. ";
        advice += kUpdateDriver;
        break;
    case StartupProblem::Other:
        headline = "Hailstone stopped because of an error.";
        advice = "If it happens again, please report it with the log named below.";
        break;
    }
    std::string text = headline + "\n\n" + advice + "\n\nWhat went wrong:\n" + std::string(what);
    if (!log.empty()) {
        // UTF-8, as the message box takes it; string() throws on Windows for a name the code page cannot hold.
        const std::u8string u8 = log.u8string();
        text += "\n\nThe log has more: " + std::string(u8.begin(), u8.end());
    }
    return text;
}

} // namespace nmsc::app
