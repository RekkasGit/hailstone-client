// This process as the system sees it (platform/process.h): its id and memory, the log's lock file, a C file by path.
#include <doctest/doctest.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "platform/process.h"
#include "scratch_dir.h"

using namespace nmsc;

TEST_CASE("process: an id, and resident memory where the system tells it") {
    CHECK(platform::process_id() != 0);
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
    auto rss = platform::resident_memory();
    REQUIRE(rss);
    CHECK(*rss > 1024u * 1024u); // a test binary holds more than a megabyte
#endif
    platform::lower_current_thread_priority(); // (this thread only; nothing to check but that it returns)
    CHECK_FALSE(platform::system_description().empty());
}

TEST_CASE("process: a lock file is held once; a second holder is turned away") {
    test::ScratchDir dir("process_lock");
    const auto lock = dir.path() / "hailstone.lock";
    CHECK(platform::hold_lock_file(lock));
    // A second open of it (a second client, here the same process again) finds it taken.
    CHECK_FALSE(platform::hold_lock_file(lock));
    // A folder that does not exist: no lock can be made, which counts as held.
    CHECK(platform::hold_lock_file(dir.path() / "missing" / "x.lock"));
}

TEST_CASE("process: a C file opened by a path with any characters in it") {
    test::ScratchDir dir("process_fopen");
    const auto path = dir.path() / std::filesystem::path(u8"log-ä日本.txt");
    std::FILE* f = platform::open_c_file(path, "wb");
    REQUIRE(f);
    std::fputs("x", f);
    std::fclose(f);
    CHECK(std::filesystem::file_size(path) == 1);
}
