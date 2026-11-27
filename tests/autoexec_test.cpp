// MacroQuest's .cfg files (app/autoexec.h): the lookup, the lines, and when Automation runs them.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/autoexec.h"
#include "app/automation.h"
#include "core/path_utf8.h"
#include "mq/lua_mq.h"
#include "mq_test_world.h"
#include "net/server_files.h"
#include "plugin/hooks.h"
#include "plugin/policy.h"
#include "scratch_dir.h"

using namespace hailstone;
namespace fs = std::filesystem;

namespace {

void write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

app::CfgFolders folders_in(const fs::path& root) {
    app::CfgFolders f;
    f.data = root;
    f.global = root / "cfg";
    f.server = root / "servers" / "p" / "cfg";
    f.character = root / "servers" / "p" / "characters" / "w" / "Tester" / "cfg";
    return f;
}

} // namespace

TEST_CASE("cfg files: MacroQuest's name, and the lines a file runs") {
    CHECK(app::cfg_file_name("InGame") == "InGame.cfg");
    CHECK(app::cfg_file_name("MQ2Nav-AutoExec") == "MQ2Nav-AutoExec.cfg");
    CHECK(app::cfg_file_name("notes.txt") == "notes.txt"); // a name with a '.' is taken as it is
    // Line ends of either kind, a byte order mark, comments (';' MacroQuest's, '#' Hailstone's), blank and indented lines.
    const auto lines = app::cfg_lines("\xEF\xBB\xBF/echo one\r\n; a comment\n\n   \n# another\n  /echo two\r\n/echo three");
    CHECK(lines == std::vector<std::string>{"/echo one", "/echo two", "/echo three"});
    CHECK(app::cfg_lines("").empty());
    CHECK(app::cfg_lines("\r\n\r\n").empty());
    CHECK(app::cfg_lines("/echo a ; not a comment") == std::vector<std::string>{"/echo a ; not a comment"});
}

TEST_CASE("cfg files: only the most specific layer's file runs, autoexec/ first, in any case") {
    test::ScratchDir scratch("cfg_layers");
    const auto f = folders_in(scratch.path());
    CHECK_FALSE(app::find_cfg(f, "InGame"));
    write(f.global / "InGame.cfg", "/echo global\n");
    CHECK(app::find_cfg(f, "InGame") == f.global / "InGame.cfg");
    CHECK(app::find_cfg(f, "ingame") == f.global / "InGame.cfg"); // any case, as on Windows
    write(f.global / "Autoexec" / "INGAME.CFG", "/echo global autoexec\n");
    CHECK(app::find_cfg(f, "InGame") == f.global / "Autoexec" / "INGAME.CFG");
    write(f.server / "ingame.cfg", "/echo server\n");
    CHECK(app::find_cfg(f, "InGame") == f.server / "ingame.cfg");
    write(f.character / "InGame.cfg", "/echo character\n");
    CHECK(app::find_cfg(f, "InGame") == f.character / "InGame.cfg");
    // A layer that is not known is not looked in.
    auto no_character = f;
    no_character.character.clear();
    CHECK(app::find_cfg(no_character, "InGame") == f.server / "ingame.cfg");
    // A name that would leave the folder is no name.
    for (const char* bad : {"", ".", "..", "../InGame", "sub/InGame", "sub\\InGame", "C:InGame", "a\nb"}) {
        CAPTURE(bad);
        CHECK_FALSE(app::find_cfg(f, bad));
    }
    // A folder of that name is not a file.
    fs::create_directories(f.global / "zoned.cfg");
    CHECK_FALSE(app::find_cfg(f, "zoned"));
}

TEST_CASE("cfg files: /loadcfg's path stays inside the data folder; a server's download never lands in cfg/") {
    test::ScratchDir scratch("cfg_paths");
    const auto f = folders_in(scratch.path());
    write(scratch.path() / "extra" / "fights.cfg", "/echo fights\n");
    CHECK(app::resolve_cfg(f, "extra/fights") == fs::weakly_canonical(scratch.path() / "extra" / "fights.cfg"));
    CHECK(app::resolve_cfg(f, "extra/fights.cfg").has_value());
    CHECK(app::resolve_cfg(f, path_utf8(scratch.path() / "extra" / "fights.cfg")).has_value()); // absolute, inside
    write(scratch.path().parent_path() / (path_utf8(scratch.path().filename()) + "-outside") / "evil.cfg", "/echo no\n");
    CHECK_FALSE(app::resolve_cfg(f, "../" + path_utf8(scratch.path().filename()) + "-outside/evil"));
    CHECK_FALSE(app::resolve_cfg(f, "extra/missing"));
    write(f.global / "InGame.cfg", "/echo global\n");
    CHECK(app::resolve_cfg(f, "InGame") == f.global / "InGame.cfg"); // a plain name: the layers
    fs::remove_all(scratch.path().parent_path() / (path_utf8(scratch.path().filename()) + "-outside"));
    // Whatever path a server lists, its file goes to assets/, scripts/, plugins/ or hailstone.cfg: never a cfg/ folder.
    CHECK(net::server_file_destination("cfg/InGame.cfg") == "assets/cfg/InGame.cfg");
    CHECK(net::server_file_destination("characters/w/Tester/cfg/zoned.cfg") == "assets/characters/w/Tester/cfg/zoned.cfg");
}

namespace {

// Automation as the client builds it, its hooks given to the connection, so the cfg lines' /echo come back as chat.
struct CfgHost {
    std::unique_ptr<app::Automation> automation;
    std::optional<mq_test::World> world;
    fs::path root;
    explicit CfgHost(const fs::path& scratch) : root(scratch) {
        app::AutomationEnv env;
        env.data_dir = root / "data";
        env.install_dir = root / "install";
        env.connection = [this] { return world ? world->conn.get() : nullptr; };
        env.server_name = [] { return std::string("TestServer"); };
        env.server_dir = [this] { return root / "data" / "servers" / "p"; };
        env.character_dir = [this] { return root / "data" / "servers" / "p" / "characters" / "w" / "Tester"; };
        automation = std::make_unique<app::Automation>(std::move(env));
        net::ConnectionOptions options;
        options.hooks = &automation->hooks();
        world.emplace("hailstone_autoexec_test", options);
        net::GameConnectionTestAccess::set_server_rules(*world->conn, plugin::Rules{}); // no hailstone.cfg: free
    }
    ~CfgHost() { automation->shutdown(); }
    fs::path global() const { return root / "data" / "cfg"; }
    fs::path character() const { return root / "data" / "servers" / "p" / "characters" / "w" / "Tester" / "cfg"; }
    std::vector<std::string> frame() {
        automation->frame();
        std::vector<std::string> out;
        while (auto e = world->conn->poll_chat()) out.push_back(e->text);
        return out;
    }
};

using Lines = std::vector<std::string>;

} // namespace

TEST_CASE("cfg files: AutoExec once, CharSelect each time, InGame and the rest at world entry, zoned at each zone-in") {
    test::ScratchDir scratch("cfg_events");
    CfgHost h(scratch.path());
    for (const char* name : {"AutoExec", "CharSelect", "InGame", "TestServer_Tester", "Cleric", "zoned", "qeynos2"})
        write(h.global() / app::cfg_file_name(name), std::string("/echo ") + name + "\n");
    // The character's own InGame.cfg is the one that runs, not the global one as well.
    write(h.character() / "InGame.cfg", "; the character's\n/echo InGame (Tester)\n");
    plugin::Hooks& hooks = h.automation->hooks();
    h.frame(); // (nothing yet)

    hooks.game_state(plugin::kGameStateCharSelect);
    CHECK(h.frame() == Lines{"AutoExec", "CharSelect"});
    // The first zone-in comes before the game state says "in game": its files wait for the world entry's.
    hooks.game_state(plugin::kGameStateLoggingIn);
    hooks.zone(plugin::ZonePhase::End);
    CHECK(h.frame().empty());
    hooks.game_state(plugin::kGameStateInGame);
    // MacroQuest's order: the zone's own file at once, zoned.cfg a pulse later.
    CHECK(h.frame() == Lines{"InGame (Tester)", "TestServer_Tester", "Cleric", "qeynos2"});
    CHECK(h.frame() == Lines{"zoned"});
    CHECK(h.frame().empty());

    // Zoning while in game: the zone's and zoned, not InGame again.
    hooks.zone(plugin::ZonePhase::Begin);
    hooks.zone(plugin::ZonePhase::End);
    CHECK(h.frame() == Lines{"qeynos2"});
    CHECK(h.frame() == Lines{"zoned"});

    // Back to character select: CharSelect, no second AutoExec; the next entry runs InGame again.
    hooks.game_state(plugin::kGameStateCharSelect);
    CHECK(h.frame() == Lines{"CharSelect"});
    hooks.game_state(plugin::kGameStateInGame);
    CHECK(h.frame() == Lines{"InGame (Tester)", "TestServer_Tester", "Cleric"});
}

TEST_CASE("cfg files: /loadcfg runs a file's lines at once, parsed, and says when there is none") {
    test::ScratchDir scratch("cfg_loadcfg");
    CfgHost h(scratch.path());
    write(h.global() / "buffs.cfg", "/echo ${Me.Name} buffs\n/loadcfg more\n");
    write(h.character() / "more.cfg", "/echo more\n");
    h.world->conn->send_command("/loadcfg buffs");
    CHECK(h.frame() == Lines{"Tester buffs", "more"});
    h.world->conn->send_command("/loadcfg nope");
    CHECK(h.frame() == Lines{"Could not /loadcfg 'nope'"});
    h.world->conn->send_command("/loadcfg");
    CHECK(h.frame() == Lines{"Usage: /loadcfg <filename>"});
    // A file that loads itself stops at the nesting limit instead of running for ever.
    write(h.global() / "loop.cfg", "/loadcfg loop\n");
    h.world->conn->send_command("/loadcfg loop");
    const auto said = h.frame();
    REQUIRE(said.size() == 1);
    CHECK(said[0].find("nest at most") != std::string::npos);
}

TEST_CASE("cfg files: a script's start runs its <name>-AutoExec.cfg") {
    test::ScratchDir scratch("cfg_script_autoexec");
    CfgHost h(scratch.path());
    write(h.root / "data" / "scripts" / "hello.lua", "local mq = require('mq')\nmq.delay(100000)\n");
    write(h.global() / "hello-AutoExec.cfg", "/echo hello's cfg\n");
    h.frame();
    REQUIRE(h.automation->scripts().run("hello", {}));
    const auto said = h.frame();
    CHECK(std::find(said.begin(), said.end(), "hello's cfg") != said.end());
}
