// The UI font's faces (ui/font_faces.h): the 'name' table's family and style, and a family's regular and bold files.
#include <doctest/doctest.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "scratch_dir.h"
#include "ui/font_faces.h"

using namespace nmsc::ui;

namespace {

void be16(std::vector<unsigned char>& d, std::uint16_t v) {
    d.push_back(static_cast<unsigned char>(v >> 8));
    d.push_back(static_cast<unsigned char>(v));
}
void be32(std::vector<unsigned char>& d, std::uint32_t v) {
    be16(d, static_cast<std::uint16_t>(v >> 16));
    be16(d, static_cast<std::uint16_t>(v));
}

// A TrueType file holding only a name table with the Windows platform's family (1) and subfamily (2),
// given as UTF-16BE bytes.
std::vector<unsigned char> font_file_utf16(const std::vector<unsigned char>& f, const std::vector<unsigned char>& st) {
    std::vector<unsigned char> name;
    be16(name, 0);
    be16(name, 2);
    be16(name, 6 + 2 * 12);
    for (int i = 0; i < 2; ++i) {
        be16(name, 3);
        be16(name, 1);
        be16(name, 0x409);
        be16(name, static_cast<std::uint16_t>(i + 1));
        be16(name, static_cast<std::uint16_t>(i == 0 ? f.size() : st.size()));
        be16(name, static_cast<std::uint16_t>(i == 0 ? 0 : f.size()));
    }
    name.insert(name.end(), f.begin(), f.end());
    name.insert(name.end(), st.begin(), st.end());
    std::vector<unsigned char> d;
    be32(d, 0x00010000);
    be16(d, 1);
    be16(d, 16);
    be16(d, 0);
    be16(d, 0);
    be32(d, 0x6e616d65); // 'name'
    be32(d, 0);
    be32(d, 12 + 16);
    be32(d, static_cast<std::uint32_t>(name.size()));
    d.insert(d.end(), name.begin(), name.end());
    return d;
}

std::vector<unsigned char> utf16(const std::string& s) {
    std::vector<unsigned char> out;
    for (char c : s) {
        out.push_back(0);
        out.push_back(static_cast<unsigned char>(c));
    }
    return out;
}

std::vector<unsigned char> font_file(const std::string& family, const std::string& style) {
    return font_file_utf16(utf16(family), utf16(style));
}

} // namespace

TEST_CASE("font faces: the name table's family and style") {
    auto face = parse_font_face(font_file("Test Sans", "Bold Italic"));
    REQUIRE(face);
    CHECK(face->family == "Test Sans");
    CHECK(face->style == "Bold Italic");
    CHECK(face->bold);
    CHECK(face->italic);
    face = parse_font_face(font_file("Test Sans", "SemiBold"));
    REQUIRE(face);
    CHECK_FALSE(face->bold);
    std::vector<unsigned char> junk(64, 7);
    CHECK_FALSE(parse_font_face(junk));
}

TEST_CASE("font faces: UTF-16 surrogate pairs, and U+FFFD for an unpaired half") {
    // A (U+1D400 as D835 DC00) B; a high surrogate before 'C'; a lone low surrogate; a high one at the end.
    std::vector<unsigned char> f = {0, 'A', 0xd8, 0x35, 0xdc, 0x00, 0, 'B', 0xd8, 0x00, 0, 'C', 0xdc, 0x01, 0xd8, 0x01};
    auto face = parse_font_face(font_file_utf16(f, utf16("Regular")));
    REQUIRE(face);
    CHECK(face->family == "A\xf0\x9d\x90\x80" "B\xef\xbf\xbd" "C\xef\xbf\xbd\xef\xbf\xbd");
}

TEST_CASE("font faces: a family's regular and bold files, by name in any case") {
    std::vector<FontFace> faces;
    auto add = [&](std::string family, std::string style, std::string file) {
        FontFace f = *parse_font_face(font_file(family, style));
        f.file = file;
        faces.push_back(f);
    };
    add("Test Sans", "Bold", "bold.ttf");
    add("Test Sans", "Light", "light.ttf");
    add("Test Sans", "Regular", "regular.ttf");
    add("Test Sans", "Italic", "italic.ttf");
    add("Other", "Book", "other.ttf");
    FontFiles files = find_font_family(faces, "test sans");
    CHECK(files.regular == "regular.ttf");
    CHECK(files.bold == "bold.ttf");
    CHECK(find_font_family(faces, "Other").regular == "other.ttf");
    CHECK(find_font_family(faces, "Missing").regular.empty());
    auto families = font_families(faces);
    REQUIRE(families.size() == 2);
    CHECK(families[0] == "Other");
}

TEST_CASE("font faces: a file whose name is not ASCII is found") {
    // On Windows path::string() is in the ANSI code page and throws for a name it cannot show; the scan must not.
    nmsc::test::ScratchDir dir("font_faces");
    const std::filesystem::path file = dir.path() / std::filesystem::path(u8"Schrift-\u00e4\u65e5\u672c.ttf");
    {
        auto bytes = font_file("Test Sans", "Regular");
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    std::vector<std::filesystem::path> dirs{dir.path()};
    auto faces = scan_font_faces(dirs);
    REQUIRE(faces.size() == 1);
    CHECK(faces[0].family == "Test Sans");
    CHECK(faces[0].file == file);
}
