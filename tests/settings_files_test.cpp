// Writing the settings files and the log (core/atomic_file.h, core/config.h, core/log.h): whole files only, two
// clients sharing one hailstone.ini, and the earlier runs' logs kept.
#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "core/atomic_file.h"
#include "core/config.h"
#include "core/ini_file.h"
#include "core/log.h"

namespace fs = std::filesystem;
using namespace nmsc;

namespace {
struct TempDir {
    fs::path path;
    explicit TempDir(const char* tag) {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / (std::string("nmsc_settings_") + tag + "_" + std::to_string(stamp));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::string read(const fs::path& file) { return read_text_file(file).value_or("<missing>"); }

// The folder's entries, to see that no temporary file is left.
int entries(const fs::path& dir) {
    return static_cast<int>(std::distance(fs::directory_iterator(dir), fs::directory_iterator()));
}
} // namespace

TEST_CASE("Settings files: written whole, through a temporary file") {
    TempDir tmp("atomic");
    const fs::path file = tmp.path / "sub" / "a.ini";
    REQUIRE(write_file_atomic(file, "one\n"));
    CHECK(read(file) == "one\n");
    REQUIRE(write_file_atomic(file, "two\n"));
    CHECK(read(file) == "two\n");
    CHECK(entries(file.parent_path()) == 1);
    // A folder in the way: nothing written, the old file as it was.
    fs::create_directories(tmp.path / "dir.ini");
    CHECK_FALSE(write_file_atomic(tmp.path / "dir.ini", "x"));
    CHECK(fs::is_directory(tmp.path / "dir.ini"));

    IniFile ini;
    ini.set("S", "k", "v");
    REQUIRE(ini.save(file));
    CHECK(read(file) == "[S]\nk=v\n");
    CHECK(entries(file.parent_path()) == 1);
}

TEST_CASE("Settings files: two clients with one hailstone.ini keep each other's changes") {
    TempDir tmp("merge");
    const fs::path file = tmp.path / "hailstone.ini";
    {
        Config first;
        first.set("ui.name", "classic");
        first.set("assets.path", "/eq");
        first.set("keys.jump", "space");
        REQUIRE(first.save(file));
    }
    Config a, b;
    REQUIRE(a.load(file));
    REQUIRE(b.load(file));
    // A changes the asset folder and saves; B, started before, changes a key binding and the server.
    a.set("assets.path", "/rof2");
    REQUIRE(a.save(file));
    b.set("keys.jump", "j");
    b.set("login.profile", "nms");
    b.set("ui.name", "classic"); // the same value: not a change
    b.erase("missing.key");
    REQUIRE(b.save(file));
    Config disk;
    REQUIRE(disk.load(file));
    CHECK(disk.get_string("assets.path") == "/rof2"); // A's change is not undone
    CHECK(disk.get_string("keys.jump") == "j");
    CHECK(disk.get_string("login.profile") == "nms");
    CHECK(disk.get_string("ui.name") == "classic");
    // B now holds the merged keys, and erasing works through a merge too.
    CHECK(b.get_string("assets.path") == "/rof2");
    a.erase("keys.jump"); // A never saw "j": its erase still goes over it
    a.set("audio.volume", "5");
    REQUIRE(a.save(file));
    REQUIRE(disk.load(file));
    CHECK_FALSE(disk.get("keys.jump"));
    CHECK(disk.get_string("login.profile") == "nms");
    CHECK(disk.get_string("audio.volume") == "5");

    // One program alone writes everything it holds, as always: an erase removes the key.
    Config alone;
    REQUIRE(alone.load(file));
    alone.erase("audio.volume");
    REQUIRE(alone.save(file));
    REQUIRE(disk.load(file));
    CHECK_FALSE(disk.get("audio.volume"));
    // A Config never read from the file replaces it.
    Config fresh;
    fresh.set("only", "1");
    REQUIRE(fresh.save(file));
    REQUIRE(disk.load(file));
    CHECK(disk.entries().size() == 1);
}

TEST_CASE("Settings files: the log keeps the earlier runs' files") {
    TempDir tmp("log");
    const fs::path file = tmp.path / "logs" / "test.log";
    for (int run = 1; run <= 5; ++run) {
        REQUIRE(log::open_file(file));
        CHECK(log::file_path() == file); // this process holds the lock: the file itself, every time
        log::info("run {}", run);
        log::close_file();
    }
    CHECK(read(file).find("run 5") != std::string::npos);
    CHECK(read(tmp.path / "logs" / "test.1.log").find("run 4") != std::string::npos);
    CHECK(read(tmp.path / "logs" / "test.2.log").find("run 3") != std::string::npos);
    CHECK(read(tmp.path / "logs" / "test.3.log").find("run 2") != std::string::npos);
    CHECK_FALSE(fs::exists(tmp.path / "logs" / "test.4.log"));
}
