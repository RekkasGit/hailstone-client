#pragma once

// Dear ImGui context and SDL3/Vulkan backends for the client and Lua interfaces. See docs/dev-ui.md.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <volk.h>

struct ImFont;
struct ImFontAtlas;
struct ImFontConfig;

namespace hailstone::platform { class Window; }
namespace hailstone::render { class VkContext; struct FrameInfo; }

namespace hailstone::ui {

namespace detail {
// Shared by the default, sized UI, chat and nameplate fonts.
ImFontConfig ui_font_config();
bool build_ui_font_atlas(ImFontAtlas& atlas);

// The fonts Hailstone ships in fonts/ for the plugins and scripts that name MacroQuest's (HSMono's E3): Roboto 3 as
// "RobotoRegular" at 16 px and "RobotoRegular (Large)" at 22 px, both with FontAwesome 4.7's icons merged in, as
// MacroQuest's ConfigureDefaultFont and ConfigureLargeFont make them (MQ's Roboto is 2.0, Apache-2.0; this one is
// 3, under the OFL, as FontAwesome's font is). Times `scale`. A missing file adds nothing of it.
inline constexpr char kRobotoFontName[] = "RobotoRegular";
inline constexpr char kRobotoLargeFontName[] = "RobotoRegular (Large)";
void add_bundled_fonts(ImFontAtlas& atlas, const std::filesystem::path& dir, float scale);

// The current context's settings file from `ini_path` (io.IniFilename points into it) to `to` (UTF-8; empty: none),
// outside a frame. Once ImGui has loaded its settings, those of the file left are saved to it first; then a `to` that
// exists is loaded in their place (ImGui moves the windows already open to its places), and one that does not yet
// starts from them (a character's first entry keeps the places it had) and is written soon. Before that first load
// only the name changes: NewFrame loads it.
void switch_ini_settings(std::string& ini_path, const std::string& to);

// A child window or popup can hold navigation focus for the tracked gamepad window.
bool pad_window_has_focus(const char* name);

// The context-only part of presentation visibility is also usable without SDL or a Vulkan device.
class ImGuiVisibility {
public:
    void set_visible(bool visible);
    bool visible() const { return visible_; }
    void prepare_input(); // before and after NewFrame; hidden input/capture never survives to restoration
    bool finish_frame(bool render); // closes the frame; false means there is no draw data to submit
    bool wants_mouse() const;
    bool wants_keyboard(int requested) const;

private:
    bool visible_ = true;
};
} // namespace detail

struct ImGuiLayerDesc {
    float font_size = 15.0f;          // in points at 100% display scale
    std::filesystem::path font_path;  // TTF to use; empty tries common system fonts, then ImGui's built-in font
    float scale_override = 0.0f;      // > 0 replaces the display content scale
    // Where ImGui keeps its windows' places and sizes (the client's own windows; the Lua windows keep theirs in
    // the character's UI ini); empty keeps none.
    std::filesystem::path ini_path;
    // The shipped fonts/ folder (detail::add_bundled_fonts); empty adds none.
    std::filesystem::path bundled_fonts_dir;
};

// A chat tab's own font face (ImGuiLayer::set_chat_faces): its family, as the font list names it, and its files.
struct ChatFaceFiles {
    std::string family;
    std::filesystem::path regular, bold; // bold empty: the regular one drawn for the bold chat fonts
    bool operator==(const ChatFaceFiles&) const = default;
};

// One per process. Construct after the VkContext; destroy before it.
class ImGuiLayer {
public:
    ImGuiLayer(platform::Window& window, render::VkContext& vk, const ImGuiLayerDesc& desc = {});
    ~ImGuiLayer();
    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    // Starts a UI frame. Call once per loop iteration after Window::poll_events,
    // then issue ImGui calls, then render() inside the frame's rendering.
    void new_frame();
    // Ends the frame's scene (VkContext::begin_overlay) and records the UI over it.
    // Call before VkContext::end_frame. If the frame was skipped, call
    // end_frame_without_render() instead so ImGui's frame is closed.
    void render(const render::FrameInfo& frame);
    void end_frame_without_render();
    // Presentation only: hiding retires input/drag/focus, not a window's shown state or saved layout.
    void set_visible(bool visible);
    bool visible() const { return visibility_.visible(); }

    // True while the UI uses the mouse or keyboard; the game should ignore them then. The keyboard: see
    // keyboard_for_ui.
    bool wants_mouse() const;
    bool wants_keyboard() const;

    float scale() const { return scale_; }
    // The UI font's face (/fontface, ui.font_face; ui/font_faces.h): its regular and bold files (empty: the
    // Arial-class font found), for every font from the next frame.
    void set_font_face(const std::filesystem::path& regular, const std::filesystem::path& bold);
    // The name plates' font (game/nameplate_style.h): its face's regular and bold files (empty: the UI font's), its
    // size in points at 100 % scale and whether it is bold, from the next frame (plate_font()).
    void set_plate_font(const std::filesystem::path& regular, const std::filesystem::path& bold, float size, bool bold_face);
    // Dear ImGui's settings file (window places, sizes, tables) from the next frame (detail::switch_ini_settings): the
    // character's while one is in the world, the shared one before (platform::imgui_ini).
    void set_ini_path(const std::filesystem::path& path);
    // The faces the chat tabs use besides the UI's (Hailstone: a font per tab), into the atlas from the next frame
    // (chat_face_font). A face whose file the rasteriser cannot read is left out.
    void set_chat_faces(std::vector<ChatFaceFiles> faces);
    // The minimum text size (game/accessibility.h min_text_size), in points at 100 % display scale; 0 none. From the
    // next frame: the fonts are built again no smaller, and ui/text_floor.h holds the rest.
    void set_min_text_size(float points);

    // The gamepad (a client extension, docs/gamepad.md): on, ImGui's gamepad navigation runs on the window's pad (menu
    // mode: the SDL3 backend drives it); off, ImGui gets nothing from the pad. Applied at the next new_frame. The pad's
    // pointer (platform::Window::pad_pointer, cursor mode) reaches ImGui as the mouse's place and buttons.
    void set_pad_navigation(bool on) { pad_nav_ = on; }
    // The interface locked (/safelock, game/option_commands.h): __ProcessMouseEvents gives the windows no mouse, so
    // from the next new_frame no button press or wheel (the mouse's or the pad pointer's) reaches ImGui; releases
    // still do, so nothing stays held.
    void set_mouse_locked(bool on) { mouse_locked_ = on; }
    // Gives ImGui's focus (and the navigation cursor) to the window of that name once it exists (a gamepad window
    // asking for the pad); a request no window answers within two seconds of frames is dropped.
    void request_window_focus(std::string name);
    bool window_focus_pending() const { return !focus_request_.empty(); }
    // The windows that asked for the pad (request_window_focus), newest last, while they are still drawn: whether one
    // is open, whether it has the focus, and giving it the focus back (after a mouse click took it elsewhere).
    bool pad_window_open();
    bool pad_window_focused() const;
    // The window the pad's focus is on or going to (a pending request_window_focus first), "" for none.
    std::string pad_focus_window() const;
    void refocus_pad_window();
    // Testing (app/input_script.h): the focused window, the active and nav items and what ImGui captures, in a line.
    std::string debug_state() const;

private:
    void apply_pad_navigation();
    void feed_pad_pointer();
    platform::Window& window_;
    render::VkContext& vk_;
    std::string ini_path_; // io.IniFilename points into it
    std::optional<std::string> pending_ini_; // set_ini_path's, switched to before the next frame
    float last_w_ = 0.0f, last_h_ = 0.0f; // the display size of the last frame not minimized
    VkFormat color_format_ = VK_FORMAT_UNDEFINED;
    std::uint32_t min_image_count_ = 0;
    float scale_ = 1.0f;
    void build_fonts();
    float font_size_ = 15.0f;
    std::filesystem::path font_path_, face_, face_bold_;
    std::filesystem::path bundled_fonts_dir_;
    std::vector<ChatFaceFiles> chat_faces_;
    void add_chat_faces();
    std::filesystem::path plate_face_, plate_face_bold_; // set_plate_font's
    float plate_size_ = 15.0f;
    bool plate_bold_ = false;
    float min_text_points_ = 0.0f;
    bool fonts_dirty_ = false;
    bool frame_open_ = false;
    detail::ImGuiVisibility visibility_;
    int keyboard_request_ = -1; // the last frame's SetNextFrameWantCaptureKeyboard (-1: none), read before NewFrame
    bool pad_nav_ = false;
    bool mouse_locked_ = false;
    std::vector<std::string> pad_windows_; // request_window_focus's windows, newest last
    bool pad_nav_applied_ = false;
    void* nav_pad_ = nullptr;                  // the SDL_Gamepad ImGui's backend was given
    bool pointer_left_ = false, pointer_right_ = false; // the pad pointer's buttons as ImGui has them
    std::string focus_request_;
    int focus_frames_ = 0;
};

// Whether the keys are the UI's in the current ImGui frame: ImGuiIO::WantCaptureKeyboard, less an item the mouse
// holds. ImGui gives the keyboard to any active item, so a press on a button, a slider, a title bar or a window's
// background took it, and the game let go of the movement keys still held. As in the original, a click on a window
// leaves the keys the game's. An edit box being typed in, a modal dialog, an item made active from the keyboard or
// the pad, and a frame that asked for the keys (`requested` == 1: the last frame's SetNextFrameWantCaptureKeyboard,
// ui.CaptureKeyboard) keep them.
bool keyboard_for_ui(int requested);

// Called before ImGui::NewFrame: a Tab pressed while no item is active does not move ImGui's focus into a window's
// first field (the chat input), so it stays the game's. Tab still moves between fields while one is typed in.
void keep_tab_from_windows();

// The name plates' font as ImGuiLayer::set_plate_font set it (nullptr when the built-in font stands in: draw with
// ImGui's font at plate_font_size()), and its size in ImGui's pixels. plate_font_faux_bold: bold was asked for and
// the face has no bold file, so the plates strike each letter twice.
ImFont* plate_font();
float plate_font_size();
bool plate_font_faux_bold();

// The current ImGui context's keys as on Windows on every system: Ctrl is Ctrl in the windows, as in the game's own
// keys, the original client and the Ctrl-clicks the windows use (on items, loot, the spell gems). ImGui's macOS
// default would make Cmd the windows' Ctrl, with Mac-style text editing; a deliberate difference from a usual Mac
// program, so a Ctrl-click means the same in a window as in the game.
void configure_keys();

} // namespace hailstone::ui
