#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "platform/private_file.h"
#include "scratch_dir.h"

namespace fs = std::filesystem;
using nmsc::platform::write_private_file;

namespace {
std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
} // namespace

TEST_CASE("Private file: written owner-only, never over a file already there") {
    nmsc::test::ScratchDir dir("private_file");
    const fs::path key = dir.path() / "release.key";
    REQUIRE(write_private_file(key, "secret\n"));
    CHECK(read_all(key) == "secret\n");
#if !defined(_WIN32)
    const fs::perms perms = fs::status(key).permissions();
    CHECK((perms & fs::perms::all) == (fs::perms::owner_read | fs::perms::owner_write));
#endif
    CHECK_FALSE(write_private_file(key, "other\n"));
    CHECK(read_all(key) == "secret\n");
    CHECK_FALSE(write_private_file(dir.path() / "no-such-folder" / "k", "x"));
}
