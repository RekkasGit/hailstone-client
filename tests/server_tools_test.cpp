// The Python tools beside the client: server/install.py (installs the server script into an EQEmu server's
// eqemu_config.json).
// Each is run with python3 -I in a scratch folder; without python3 (or on Windows) the cases check nothing.

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "scratch_dir.h"

#if !defined(_WIN32)

namespace fs = std::filesystem;

namespace {

fs::path repo_root() {
    return fs::path(__FILE__).parent_path().parent_path();
}

std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string quote(const fs::path& p) {
    std::string s = "'";
    for (char c : p.string()) s += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return s + "'";
}

bool have_python() {
    return std::system("python3 -I -c '' >/dev/null 2>&1") == 0;
}

// Runs python3 -I `script` `args` and gives its exit code; its output goes to `out`.
int run_python(const fs::path& script, const std::string& args, const fs::path& out) {
    const fs::path code = out.string() + ".code";
    const std::string cmd = "python3 -I " + quote(script) + " " + args + " >" + quote(out) + " 2>&1; echo $? >" +
                            quote(code);
    if (std::system(cmd.c_str()) == -1) return -1;
    return std::atoi(read_all(code).c_str());
}

} // namespace

TEST_CASE("install.py keeps eqemu_config.json's mode: the database password stays private") {
    if (!have_python() || !fs::exists(repo_root() / "server" / "install.py")) return;
    nmsc::test::ScratchDir dir("install_py");
    const fs::path config = dir.path() / "eqemu_config.json";
    std::ofstream(config) << R"({"server": {"database": {"password": "x"}, "directories": {"quests": "quests/"}}})";
    fs::permissions(config, fs::perms::owner_read | fs::perms::owner_write);
    const fs::path script = repo_root() / "server" / "install.py";
    REQUIRE(run_python(script, quote(dir.path()), dir.path() / "out") == 0);
    CHECK(read_all(config).find("hailstone/quests/") != std::string::npos);
    CHECK(fs::exists(dir.path() / "hailstone" / "quests" / "global" / "script_init.lua"));
    CHECK((fs::status(config).permissions() & fs::perms::all) == (fs::perms::owner_read | fs::perms::owner_write));
    CHECK_FALSE(fs::exists(config.string() + ".tmp"));

    fs::permissions(config, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read);
    REQUIRE(run_python(script, quote(dir.path()) + " --remove", dir.path() / "out") == 0);
    CHECK(read_all(config).find("hailstone/quests/") == std::string::npos);
    CHECK_FALSE(fs::exists(dir.path() / "hailstone"));
    CHECK((fs::status(config).permissions() & fs::perms::all) ==
          (fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read));
}

#endif
