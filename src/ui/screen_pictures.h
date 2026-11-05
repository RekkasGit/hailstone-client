#pragma once

// The pictures behind the pre-game screens (login, server and character select without its scene) and the loading
// screens: any PNG, JPEG, BMP, TGA or DDS file of a loadingbackgrounds folder. A server's own,
// servers/<id>/assets/loadingbackgrounds, replaces the player's, ui/loadingbackgrounds, whenever it holds any; with
// neither, the pictures Hailstone ships (pictures/ beside the binary) are shown.

#include <filesystem>
#include <vector>

struct ImDrawList;

namespace nmsc::ui {

// The name of both folders.
inline constexpr const char* kPictureFolder = "loadingbackgrounds";

// The picture files directly in `dir` (by extension, any case), sorted by name; none when it is missing.
std::vector<std::filesystem::path> list_pictures(const std::filesystem::path& dir);

// The server's pictures when its folder has any, else the player's, else the shipped ones.
std::vector<std::filesystem::path> screen_pictures(const std::filesystem::path& client_dir,
                                                   const std::filesystem::path& server_dir,
                                                   const std::filesystem::path& shipped_dir = {});

// A picture to draw: an ImTextureID and its size in pixels.
struct ScreenPicture {
    void* id = nullptr;
    int width = 0, height = 0;
};

// Draws it over the whole main viewport, scaled to cover it: centred across and held to the top, so a wide screen
// cuts the overhang off the bottom (a title at the top stays whole) and a tall one off the sides.
void draw_screen_picture(ImDrawList* list, const ScreenPicture& picture);

} // namespace nmsc::ui
