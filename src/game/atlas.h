#pragma once

// The map window's atlas page (AtlasWnd, 0x61e2d0..0x620b80): pages of the game folder's Atlas\<skin>\ (Default.xml first), each a TinyXML document of top-level
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

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hailstone::game {

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

// The whole atlas as AtlasWnd_LoadDefaultPage @0x620760 walks it: the pages reachable from Default.xml through page
// links (ClickAction 1), in the order they are found, and every zone link (ClickAction 2) on them. The titles and
// parents are Hailstone's, for the modern map's breadcrumbs and zone finder.
struct AtlasPageEntry {
    std::string file;   // as the first link to it names it
    std::string title;  // that link's tooltip (else the file without ".xml"); "World" for Default.xml
    std::string parent; // the page that first links it; empty for Default.xml
};
struct AtlasZoneEntry {
    std::string zone; // the link's FileToOpen: the zone's short name, as written
    std::string name; // its tooltip (else the short name)
    std::string page; // the page it is on
    AtlasRect rect;
};
struct AtlasIndex {
    std::vector<AtlasPageEntry> pages;
    std::vector<AtlasZoneEntry> zones; // page by page, each page's links in order; a zone may be on several pages
    // The first page linking the zone (any case), or nullptr.
    const AtlasZoneEntry* find_zone(std::string_view short_name) const;
    const AtlasPageEntry* find_page(std::string_view file) const;
};
// `read(file)` gives a page's text (nullopt when it cannot be read; such a page is listed with no zones). At most
// 256 pages are read.
AtlasIndex index_atlas(const std::function<std::optional<std::string>(const std::string&)>& read);

} // namespace hailstone::game
