#pragma once

// The map window's atlas page (AtlasWnd, rof2decomp 0x61e2d0..0x620b80, decompiled in a scratch copy of the Ghidra
// database): pages of the game folder's Atlas\<skin>\ (Default.xml first), each a TinyXML document of top-level
// elements with unquoted attributes:
//   <Background Image = "file.tga" CX = 128 CY = 128 />          the image tiled under the page
//   <Image File = "file.tga"> <Rect X = 0 Y = 0 CX = 256 CY = 256/> </Image>   a picture (the file's top left
//                                                                  CX x CY pixels at X, Y)
//   <Label Text = "Odus" X = 45 Y = 155 [Font = n]/>               a CLabelWnd (font 3 by default), the text's extent
//                                                                  on a 0x64000000 box, white
//   <Link ClickAction = 1|2 LinkName = "" FileToOpen = "x" ToolTip = "t"> <Rect .../> [<Image File CX CY/>] </Link>
//                                                                  a button: 1 opens page x, 2 shows zone x's map
// AtlasWnd_LoadPage @0x620260 reads Background, then the Images (AtlasWnd_ParseBackgroundElement), Labels and Links in
// that order; what is wrong in an element is collected ("Rect(X Y )", "Label Error (Text )" ...) and, when anything
// was, "Atlas Page Error" goes to chat with the text in AtlasPageError.txt.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nmsc::game {

struct AtlasRect {
    int left = 0, top = 0, right = 0, bottom = 0; // AtlasWnd_ParseRectAttributes: {X, Y, X + CX, Y + CY}
};

struct AtlasImage {
    std::string file;
    AtlasRect rect;
};

struct AtlasLabel {
    std::string text;
    int x = 0, y = 0;
    int font = 3;
};

struct AtlasLink {
    int action = 0;          // ClickAction: 1 a page, 2 a zone's map
    std::string target;      // FileToOpen: the page file, or the zone's short name
    std::string name;        // LinkName
    std::string tooltip;     // ToolTip
    AtlasRect rect;
    std::optional<AtlasImage> image; // its <Image File CX CY> (drawn in the button's rectangle)
};

struct AtlasPage {
    std::optional<std::string> background; // Background Image
    int background_cx = 0, background_cy = 0;
    std::vector<AtlasImage> images;
    std::vector<AtlasLabel> labels;
    std::vector<AtlasLink> links;
    std::string errors; // empty when the page read cleanly
    // The pictures' and links' extent (the page's scrolled size).
    int width() const;
    int height() const;
};

// One page's text as AtlasWnd_LoadPage reads it.
AtlasPage parse_atlas_page(std::string_view text);

} // namespace nmsc::game
