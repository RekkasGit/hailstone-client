// game/lua_social.h: the Lua socials' file, hash and share text.
#include <doctest/doctest.h>

#include "game/lua_social.h"

using namespace hailstone;
using namespace hailstone::game;

TEST_CASE("Lua socials: the file round-trips") {
    LuaSocialFile f;
    f.name = "Assist";
    f.color = 3;
    f.lua.press = "if mq.TLO.Target() then\n    mq.cmd('/attack on')\nend";
    f.lua.face = "return {label = 'Hit'}";
    std::string text = format_lua_social(f);
    CHECK(text.starts_with("-- Hailstone Lua social\n-- name: Assist\n-- color: 3\n-- [press]\n"));
    auto back = parse_lua_social(text);
    REQUIRE(back);
    CHECK(back->name == "Assist");
    CHECK(back->color == 3);
    CHECK(back->lua == f.lua);

    f.lua.face.clear();
    text = format_lua_social(f);
    CHECK(text.find("-- [face]") == std::string::npos);
    back = parse_lua_social(text);
    REQUIRE(back);
    CHECK(back->lua.face.empty());
}

TEST_CASE("Lua socials: parsing takes CRLF, trims, cuts long names and refuses plain text") {
    auto f = parse_lua_social("-- name: A very long social name\r\n-- [press]\r\nmq.cmd('/sit')\r\n\r\n");
    REQUIRE(f);
    CHECK(f->name == "A very long soc");
    CHECK(f->lua.press == "mq.cmd('/sit')");
    CHECK_FALSE(parse_lua_social("print('no sections')"));
    CHECK(parse_lua_social("-- [face]\nreturn {}")->lua.press.empty());
}

TEST_CASE("Lua socials: the hash counts the code only, not the name, colour or line ends") {
    LuaSocial a{"mq.cmd('/sit')", ""};
    LuaSocial b{"mq.cmd('/sit')\r\n\n", ""};
    CHECK(lua_social_hash(a) == lua_social_hash(b));
    CHECK(lua_social_hash(a) != lua_social_hash(LuaSocial{"mq.cmd('/stand')", ""}));
    CHECK(lua_social_hash(a) != lua_social_hash(LuaSocial{"", "mq.cmd('/sit')"}));
    // The documented text: "-- [press]\n" press "\n-- [face]\n" face.
    CHECK(lua_social_hash(a) == sha256(std::string_view("-- [press]\nmq.cmd('/sit')\n-- [face]\n")));
}

TEST_CASE("Lua socials: share text") {
    LuaSocialFile f;
    f.name = "Héal";
    f.color = 1;
    f.lua.press = "mq.cmd('/cast 1') -- \xe2\x9c\x93";
    for (std::size_t extra = 0; extra < 3; ++extra) { // every base64 tail length
        f.lua.face = std::string(extra, 'x');
        std::string s = export_lua_social(f);
        CHECK(s.starts_with(kLuaSocialSharePrefix));
        CHECK(s.find('\n') == std::string::npos);
        auto back = import_lua_social("  " + s + "\n");
        REQUIRE(back);
        CHECK(back->name == f.name);
        CHECK(back->lua == f.lua);
    }
    CHECK_FALSE(import_lua_social("hailstone-social:1:@@@"));
    CHECK_FALSE(import_lua_social("something else"));
    CHECK(lua_social_file_name(0) == "1.lua");
    CHECK(lua_social_file_name(119) == "120.lua");
}
