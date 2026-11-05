// ui/code_syntax.h: the code editor's Lua colours.
#include <doctest/doctest.h>

#include <string>

#include "ui/code_syntax.h"

using namespace nmsc::ui;

namespace {

std::string kinds(std::string_view text) {
    std::string out;
    for (const auto& s : lua_code_spans(text)) {
        static constexpr char k[] = {'n', 'k', 's', 'c', '0', 'b', 'm', '.'};
        out += k[static_cast<int>(s.kind)];
    }
    return out;
}

} // namespace

TEST_CASE("code syntax: names, keywords, members, built-ins, numbers") {
    CHECK(kinds("local x = mq.TLO.Me.PctHPs()") == "kn.b.m.m.m..");
    CHECK(kinds("if a .. b then return 1.5e-3 end") == "kn.nkk0k");
    CHECK(kinds("state.n = 0x1F") == "b.m.0");
    CHECK(kinds("obj:method(...)") == "n.m...");
}

TEST_CASE("code syntax: strings and comments, long brackets across lines") {
    auto spans = lua_code_spans("print('a\\'b') -- note\nx = [==[\nlong]] still]==] y");
    REQUIRE(spans.size() == 9);
    CHECK(spans[2].kind == CodeToken::String);
    CHECK(spans[2].end - spans[2].begin == 6);
    CHECK(spans[4].kind == CodeToken::Comment);
    CHECK(spans[7].kind == CodeToken::String);
    CHECK(spans[8].kind == CodeToken::Name);
    CHECK(kinds("--[[ a\nb ]] c") == "cn");
    CHECK(kinds("s = \"unterminated\nmore") == "n.sn");
}
