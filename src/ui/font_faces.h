#pragma once

// The system's fonts by face name, for the UI font (/fontface, the Options window's font list). The original asks
// Windows for a face by name (CreateFontIndirectA with the LOGFONT's lfFaceName; the Options window's list is
// EnumFontFamiliesEx's, EnumerateSystemFonts @0x5fedb0); here the font files in the system's font folders are read
// for their family and style names (the TrueType / OpenType 'name' table) and ImGui loads the file.

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nmsc::ui {

struct FontFace {
    std::string family; // the typographic family (name 16), else the family (name 1)
    std::string style;  // the typographic subfamily (name 17), else the subfamily (name 2): "Regular", "Bold", ...
    std::filesystem::path file;
    bool bold = false, italic = false; // from the style's words
};

// The face a TrueType / OpenType file (.ttf, .otf; the first font of a .ttc) names; nullopt when it is not one.
std::optional<FontFace> read_font_face(const std::filesystem::path& file);
// The same from the file's bytes.
std::optional<FontFace> parse_font_face(std::span<const unsigned char> data);

// The font folders of this system: Windows' Fonts folder, macOS' Library/Fonts folders, the XDG font folders.
std::vector<std::filesystem::path> system_font_dirs();
// Every face in `dirs` (and their subfolders), by family then style. Read once per call: the caller keeps them.
std::vector<FontFace> scan_font_faces(std::span<const std::filesystem::path> dirs);
// The family names, once each, sorted without regard to case.
std::vector<std::string> font_families(std::span<const FontFace> faces);

// A family's upright regular and bold files (case-insensitive name; empty paths when there is no such face). The
// regular one is the face whose style is "Regular" (else any upright, not bold one); the bold one the upright "Bold".
struct FontFiles {
    std::filesystem::path regular, bold;
};
FontFiles find_font_family(std::span<const FontFace> faces, std::string_view family);

} // namespace nmsc::ui
