#include <doctest/doctest.h>

#include <map>
#include <optional>
#include <string>

#include "game/atlas.h"
#include "game_files.h"

using namespace hailstone;
using namespace hailstone::game;

TEST_CASE("Atlas pages: TinyXML elements with bare attribute values (AtlasWnd_LoadPage)") {
    AtlasPage p = parse_atlas_page("<Background Image = \"Atlas_bkg_256.tga\" CX = 128 CY = 128 />\n"
                                   "<Image File = \"Atlas00.tga\">\n\t<Rect X = 0 Y = 0 CX = 256 CY = 256/>\n</Image>\n"
                                   "<Label Text = \"North West\" X = 120 Y = 90/>\n"
                                   "<Link ClickAction = 1 LinkName = \"\" FileToOpen = \"Odus.xml\" ToolTip = \"Odus\">\n"
                                   "\t<Rect X = 20 Y = 125 CX = 80 CY = 105/>\n</Link>"
                                   "<Image File = \"map_icon.tga\">\n<Rect X = 0 Y = 0 CX = 64 CY = 64/></Image>\n"
                                   "<Link ClickAction = 2 FileToOpen = \"kaladimb\" ToolTip = \"North Kaladim\">"
                                   "<Rect X = 70 Y = 95 CX = 50 CY = 40/><Image File = \"x.tga\" CX = 10 CY = 12/></Link>"
                                   "<Label X = 1 Y = 2/>");
    REQUIRE(p.background);
    CHECK(*p.background == "Atlas_bkg_256.tga");
    CHECK(p.background_cx == 128);
    REQUIRE(p.images.size() == 2);
    CHECK(p.images[0].file == "Atlas00.tga");
    CHECK(p.images[0].rect.right == 256);
    REQUIRE(p.labels.size() == 1);
    CHECK(p.labels[0].text == "North West");
    CHECK(p.labels[0].font == 3);
    REQUIRE(p.links.size() == 2);
    CHECK(p.links[0].action == 1);
    CHECK(p.links[0].target == "Odus.xml");
    CHECK(p.links[0].rect.left == 20);
    CHECK(p.links[0].rect.bottom == 230);
    CHECK(p.links[1].action == 2);
    CHECK(p.links[1].target == "kaladimb");
    REQUIRE(p.links[1].image);
    CHECK(p.links[1].image->rect.right == 80);
    CHECK(p.errors == "Label Error (Text )");
    CHECK(p.width() == 256);
}

TEST_CASE("Atlas pages: the game's Atlas\\Default pages read cleanly") {
    const GameDir* g = test::game_dir();
    if (!g) return;
    for (const char* f : {"Default.xml", "Faydwer.xml", "Kunark.xml", "Planes.xml"}) {
        auto data = g->read(std::string("Atlas/Default/") + f);
        if (!data) continue;
        AtlasPage p = parse_atlas_page(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()));
        INFO(f);
        CHECK(p.errors.empty());
        CHECK_FALSE(p.images.empty());
        CHECK_FALSE(p.links.empty());
    }
}

TEST_CASE("Atlas index: the pages reachable from Default.xml and their zone links (AtlasWnd_LoadDefaultPage)") {
    std::map<std::string, std::string> files = {
        {"Default.xml",
         "<Link ClickAction = 1 FileToOpen = \"Faydwer.xml\" ToolTip = \"Faydwer\"><Rect X = 0 Y = 0 CX = 9 CY = 9/></Link>"
         "<Link ClickAction = 1 FileToOpen = \"Odus.xml\"><Rect X = 0 Y = 0 CX = 9 CY = 9/></Link>"
         "<Link ClickAction = 1 FileToOpen = \"Missing.xml\" ToolTip = \"Gone\"><Rect X = 0 Y = 0 CX = 9 CY = 9/></Link>"},
        {"Faydwer.xml",
         "<Link ClickAction = 1 FileToOpen = \"default.xml\" ToolTip = \"World\"><Rect X = 0 Y = 0 CX = 64 CY = 64/></Link>"
         "<Link ClickAction = 2 FileToOpen = \"kaladimb\" ToolTip = \"North Kaladim\"><Rect X = 70 Y = 95 CX = 50 CY = 40/></Link>"
         "<Link ClickAction = 2 FileToOpen = \"gfaydark\"><Rect X = 1 Y = 2 CX = 3 CY = 4/></Link>"},
        {"Odus.xml",
         "<Link ClickAction = 2 FileToOpen = \"Erudnext\" ToolTip = \"Erudin\"><Rect X = 5 Y = 6 CX = 7 CY = 8/></Link>"
         "<Link ClickAction = 1 FileToOpen = \"FAYDWER.XML\"><Rect X = 0 Y = 0 CX = 9 CY = 9/></Link>"},
    };
    AtlasIndex index = index_atlas([&](const std::string& f) -> std::optional<std::string> {
        auto it = files.find(f);
        return it == files.end() ? std::nullopt : std::optional<std::string>(it->second);
    });
    REQUIRE(index.pages.size() == 4); // Default, Faydwer, Odus, Missing: each page once, in any case
    CHECK(index.pages[0].file == "Default.xml");
    CHECK(index.pages[0].title == "World");
    CHECK(index.pages[0].parent.empty());
    CHECK(index.pages[1].title == "Faydwer");
    CHECK(index.pages[1].parent == "Default.xml");
    CHECK(index.pages[2].title == "Odus"); // no tooltip: the file without ".xml"
    CHECK(index.pages[3].title == "Gone");
    REQUIRE(index.zones.size() == 3);
    CHECK(index.zones[0].zone == "kaladimb");
    CHECK(index.zones[0].name == "North Kaladim");
    CHECK(index.zones[0].page == "Faydwer.xml");
    CHECK(index.zones[0].rect.right == 120);
    CHECK(index.zones[1].name == "gfaydark"); // no tooltip: the short name
    CHECK(index.zones[2].page == "Odus.xml");
    REQUIRE(index.find_zone("ERUDNEXT"));
    CHECK(index.find_zone("ERUDNEXT")->name == "Erudin");
    CHECK(index.find_zone("qeynos") == nullptr);
    REQUIRE(index.find_page("odus.xml"));
    CHECK(index.find_page("odus.xml")->file == "Odus.xml");
}

TEST_CASE("Atlas index: the game's atlas links its zones") {
    const GameDir* g = test::game_dir();
    if (!g) return;
    AtlasIndex index = index_atlas([&](const std::string& f) -> std::optional<std::string> {
        auto data = g->read("Atlas/Default/" + f);
        if (!data) return std::nullopt;
        return std::string(reinterpret_cast<const char*>(data->data()), data->size());
    });
    CHECK(index.pages.size() > 10);
    CHECK(index.zones.size() > 100);
    CHECK(index.find_zone("gfaydark") != nullptr);
}
