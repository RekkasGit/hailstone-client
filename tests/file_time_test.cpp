#include <doctest/doctest.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>

#include "core/file_time.h"
#include "scratch_dir.h"

namespace fs = std::filesystem;

TEST_CASE("File time: a file's last write as calendar time") {
    hailstone::test::ScratchDir dir("file_time");
    const fs::path file = dir.path() / "a.txt";
    std::ofstream(file) << "x";
    const std::time_t before = std::time(nullptr);
    // Set explicitly: a time the file system stores, an hour ago, read back through the conversion.
    const auto hour_ago = fs::file_time_type::clock::now() - std::chrono::hours(1);
    fs::last_write_time(file, hour_ago);
    const std::time_t t = hailstone::file_time_to_time_t(fs::last_write_time(file));
    CHECK(t <= before - 3600 + 2);
    CHECK(t >= before - 3600 - 5);
    // The time_point form agrees with the time_t one.
    CHECK(std::chrono::system_clock::to_time_t(hailstone::file_time_to_system(fs::last_write_time(file))) == t);
}
