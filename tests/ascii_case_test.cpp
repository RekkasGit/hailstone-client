// core/ascii_case.h: the portable stand-ins for strcasecmp and fnmatch.
#include <doctest/doctest.h>

#include "core/ascii_case.h"

using namespace nmsc;

TEST_CASE("icompare orders as strcasecmp does in the C locale") {
    CHECK(icompare("Apple", "apple") == 0);
    CHECK(icompare("apple", "Banana") < 0);
    CHECK(icompare("BANANA", "apple") > 0);
    CHECK(icompare("abc", "ABCD") < 0); // a prefix sorts first
    CHECK(icompare("", "") == 0);
    CHECK(icompare("[x]", "a") < 0);       // '[' (0x5b) is not folded
    CHECK(icompare("\xe9", "\xc9") > 0);   // bytes above 127 compare unsigned and unfolded
    CHECK(icompare("_", "a") < 0);         // '_' (0x5f) < 'a' (0x61): compared after folding, as strcasecmp
    CHECK(icompare("_", "A") < 0);
}

TEST_CASE("istarts_with, iends_with, iequals") {
    CHECK(istarts_with("#extinf:12,Song", "#EXTINF:"));
    CHECK_FALSE(istarts_with("#EXT", "#EXTINF:"));
    CHECK(iends_with("list.M3U", ".m3u"));
    CHECK_FALSE(iends_with("m3u", ".m3u"));
    CHECK(iequals("Add", "add"));
    CHECK_FALSE(iequals("add", "adds"));
}

TEST_CASE("iglob matches * and ? ignoring case, nothing else") {
    CHECK(iglob("*.mp3", "a.MP3"));
    CHECK(iglob("*.mp3", "b song.mp3"));
    CHECK_FALSE(iglob("*.mp3", "c.wav"));
    CHECK_FALSE(iglob("*.mp3", "x.mp3.txt"));
    CHECK(iglob("?.mp3", "a.mp3"));
    CHECK_FALSE(iglob("?.mp3", "ab.mp3"));
    CHECK(iglob("*", ""));
    CHECK(iglob("a*b*c", "aXXbYYbc"));
    CHECK_FALSE(iglob("a*b*c", "aXXbYY"));
    CHECK(iglob("**x", "x"));
    CHECK(iglob("[1].mp3", "[1].MP3")); // brackets are literal, as in FindFirstFile
    CHECK_FALSE(iglob("[1].mp3", "1.mp3"));
}
