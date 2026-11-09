// The character's own files (core/character_files.h): their folder under the server's profile, the chat settings
// over the asset folder's eqclient.ini, and the saves leaving the asset folder as it was.
#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

#include "core/character_files.h"
#include "core/game_dir.h"
#include "core/ini_file.h"
#include "ui/ui_ini.h"

#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace hailstone;

namespace {
struct TempDir {
    fs::path path;
    explicit TempDir(const char* tag) {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / (std::string("hailstone_charfiles_") + tag + "_" + std::to_string(stamp));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void write(const fs::path& file, std::string_view text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

// Every file under `root` with its contents, to compare before and after.
std::map<std::string, std::string> snapshot(const fs::path& root) {
    std::map<std::string, std::string> out;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        if (!e.is_regular_file()) continue;
        std::ifstream in(e.path(), std::ios::binary);
        out[fs::relative(e.path(), root).generic_string()] = std::string(std::istreambuf_iterator<char>(in), {});
    }
    return out;
}

// A small asset folder: eqclient.ini with chat settings, defaults.ini and the game's own UI ini for the seed.
void fake_asset_folder(const fs::path& root) {
    write(root / "eqclient.ini", "[Options]\nPCSpells=1\nBadWord=1\n[TextColors]\nUser_1_Red=10\n");
    write(root / "defaults.ini", "[PlayerWindow]\nShow=1\n");
    write(root / "UI_Gennro_NMS Dev Server.ini", "[PlayerWindow]\nXPosWindowed=12\n");
}
} // namespace

TEST_CASE("Character files: the profile of a world without one, and the files' names") {
    CHECK(profile_id_for_world("NMS Dev Server") == "nms-dev-server");
    CHECK(profile_id_for_world("  My  Server!! ") == "my-server");
    CHECK(profile_id_for_world("!!!") == "server");
    CHECK(profile_id_for_world("") == "server");
    CHECK(profile_id_for_world("Con") == "_con");

    fs::path servers = "/data/servers";
    CharacterFiles f = character_files("nms-dev-server", "NMS Dev Server", "Gennro", servers);
    fs::path dir = servers / "nms-dev-server" / "characters" / "NMS Dev Server" / "Gennro";
    CHECK(f.dir == dir);
    CHECK(f.ui_ini() == dir / "UI.ini");
    CHECK(f.hot_buttons() == dir / "hotbuttons.ini");
    CHECK(f.trader_prices() == dir / "BZR.ini");
    CHECK(f.address_book() == dir / "AddressBook.txt");
    CHECK(f.chat_ini() == dir / "chat.ini");
    // One login, two worlds: a character of the same name on each has files of its own.
    CHECK(character_files("nms-dev-server", "NMS Test", "Gennro", servers).dir ==
          servers / "nms-dev-server" / "characters" / "NMS Test" / "Gennro");
    // A world's name made a folder name.
    CHECK(character_files("p", "Main: PvP/RP", "Gennro", servers).dir == servers / "p" / "characters" / "Main_ PvP_RP" / "Gennro");
    // No character (or no world, or no profile) yet: no files at all.
    CHECK(character_files("nms-dev-server", "NMS Dev Server", "", servers).empty());
    CHECK(character_files("nms-dev-server", "", "Gennro", servers).empty());
    CHECK(character_files("", "NMS Dev Server", "Gennro", servers).chat_ini().empty());
}

TEST_CASE("Character files: chat.ini over eqclient.ini, and saves only into chat.ini") {
    TempDir game_root("game"), data("data");
    fake_asset_folder(game_root.path);
    auto before = snapshot(game_root.path);
    GameDir game(game_root.path);
    fs::path chat = character_files("test", "Test", "Gennro", data.path / "servers").chat_ini();

    // No chat.ini yet: eqclient.ini's values are the defaults.
    IniFile first = load_chat_settings(game, chat);
    CHECK(first.get_int("Options", "PCSpells", -1) == 1);
    CHECK(first.get_int("TextColors", "User_1_Red", -1) == 10);

    REQUIRE(save_chat_setting(chat, "Options", "PCSpells", "2"));
    REQUIRE(save_chat_settings(chat, "TextColors", {{"User_1_Red", "200"}, {"User_1_Green", "0"}}));
    IniFile after = load_chat_settings(game, chat);
    CHECK(after.get_int("Options", "PCSpells", -1) == 2);
    CHECK(after.get_int("Options", "BadWord", -1) == 1); // still eqclient.ini's
    CHECK(after.get_int("TextColors", "User_1_Red", -1) == 200);
    CHECK(after.get_int("TextColors", "User_1_Green", -1) == 0);

    // chat.ini holds only what was changed; the asset folder is as it was.
    IniFile own;
    REQUIRE(own.load(chat));
    CHECK_FALSE(own.get("Options", "BadWord"));
    CHECK(snapshot(game_root.path) == before);
    // An empty path (no character) reads the defaults and writes nothing.
    CHECK(load_chat_settings(game, {}).get_int("Options", "PCSpells", -1) == 1);
    CHECK_FALSE(save_chat_setting({}, "Options", "PCSpells", "3"));
}

TEST_CASE("Character files: the UI ini is seeded from the asset folder and saved into the character's folder") {
    TempDir game_root("game"), data("data");
    fake_asset_folder(game_root.path);
    auto before = snapshot(game_root.path);
    GameDir game(game_root.path);
    CharacterFiles files = character_files("nms-dev-server", "NMS Dev Server", "Gennro", data.path / "servers");

    ui::UiIni ini;
    ini.load(*game.find("defaults.ini"), files.ui_ini(), *game.find("UI_Gennro_NMS Dev Server.ini"));
    CHECK(ini.get("PlayerWindow", "XPosWindowed") == std::optional<std::string>("12")); // the seed's
    ini.store_shown(ui::window_ini_info("ActionsWindow"), false); // IniFlags bit 8: its shown state is kept
    REQUIRE(ini.save());

    IniFile saved;
    REQUIRE(saved.load(files.ui_ini()));
    CHECK(saved.get_int("ActionsWindow", "Show", -1) == 0);
    CHECK(saved.get_int("PlayerWindow", "XPosWindowed", -1) == 12);
    CHECK(snapshot(game_root.path) == before);
}

TEST_CASE("Character files: a test login never writes into a saved profile of another server") {
    TempDir tmp("transient");
    const fs::path servers = tmp.path / "servers";
    // No saved profile of that name: the world's own folder.
    CHECK(transient_profile_id("NMS Test", "127.0.0.1", 5999, servers) == "nms-test");
    // A saved "nms-test" for this very address: its folder (the same server, the same characters).
    write(servers / "nms-test" / "server.ini", "[Server]\nname=NMS Test\nhost=Dev.Example\nport=5999\n");
    CHECK(transient_profile_id("NMS Test", "dev.example", 5999, servers) == "nms-test");
    // The saved one is another server (another host, or port): a folder of its own.
    CHECK(transient_profile_id("NMS Test", "127.0.0.1", 5999, servers) == "_transient-nms-test");
    CHECK(transient_profile_id("NMS Test", "dev.example", 5998, servers) == "_transient-nms-test");
    CHECK(character_files("_transient-nms-test", "NMS Test", "Bob", servers).ui_ini() ==
          servers / "_transient-nms-test" / "characters" / "NMS Test" / "Bob" / "UI.ini");
}

TEST_CASE("Character files: an earlier build's characters/<name>/ is copied once to the world's folder") {
    TempDir tmp("adopt");
    const fs::path servers = tmp.path / "servers";
    const fs::path unsorted = servers / "p" / "characters" / "Bob";
    write(unsorted / "UI.ini", "[ChatWindow]\n");
    write(unsorted / "hotbuttons.ini", "buttons");
    write(unsorted / "chat.ini", "[Options]\n");
    write(unsorted / "notes.txt", "not the character's");
    const auto before = snapshot(unsorted);

    // The first entry into a world copies the character's files; the old folder stays as it was.
    CHECK(adopt_unsorted_character_files("p", "Alpha", "Bob", servers) == 3);
    CharacterFiles alpha = character_files("p", "Alpha", "Bob", servers);
    CHECK(snapshot(alpha.dir) ==
          std::map<std::string, std::string>{{"UI.ini", "[ChatWindow]\n"}, {"chat.ini", "[Options]\n"},
                                             {"hotbuttons.ini", "buttons"}});
    CHECK(snapshot(unsorted) == before);

    // Once: the world's folder exists now, and what the character changed there is kept.
    write(alpha.hot_buttons(), "changed");
    CHECK(adopt_unsorted_character_files("p", "Alpha", "Bob", servers) == 0);
    CHECK(snapshot(alpha.dir).at("hotbuttons.ini") == "changed");

    // A world whose folder of Bob exists already takes nothing.
    write(character_files("p", "Beta", "Bob", servers).ui_ini(), "beta's own");
    CHECK(adopt_unsorted_character_files("p", "Beta", "Bob", servers) == 0);
    CHECK(snapshot(servers / "p" / "characters" / "Beta" / "Bob").size() == 1);

    // No old folder (or one without the character's files, such as another world's folder): nothing is made.
    CHECK(adopt_unsorted_character_files("p", "Alpha", "Carol", servers) == 0);
    CHECK_FALSE(fs::exists(servers / "p" / "characters" / "Alpha" / "Carol"));
    CHECK(adopt_unsorted_character_files("p", "Gamma", "Alpha", servers) == 0);
    CHECK_FALSE(fs::exists(servers / "p" / "characters" / "Gamma"));
    CHECK(adopt_unsorted_character_files("", "Alpha", "Bob", servers) == 0);
}

TEST_CASE("Character files: only the first world a character of that name enters takes the earlier build's files") {
    TempDir tmp("adopt_first");
    const fs::path servers = tmp.path / "servers";
    const fs::path unsorted = servers / "p" / "characters" / "Bob";
    write(unsorted / "BZR.ini", "prices");
    write(unsorted / "hotbuttons.ini", "buttons");

    CHECK(adopt_unsorted_character_files("p", "Alpha", "Bob", servers) == 2);
    // Another world's Bob is someone else: no folder, no copies (he starts with the defaults).
    CHECK(adopt_unsorted_character_files("p", "Gamma", "Bob", servers) == 0);
    CHECK_FALSE(fs::exists(servers / "p" / "characters" / "Gamma"));
    // Another character still takes its own.
    write(servers / "p" / "characters" / "Carol" / "UI.ini", "carol");
    CHECK(adopt_unsorted_character_files("p", "Gamma", "Carol", servers) == 1);
}

#ifndef _WIN32
TEST_CASE("Character files: a copy that fails leaves no world folder, and the next entry tries again") {
    if (::geteuid() == 0) return; // root reads any file
    TempDir tmp("adopt_retry");
    const fs::path servers = tmp.path / "servers";
    const fs::path unsorted = servers / "p" / "characters" / "Bob";
    write(unsorted / "UI.ini", "windows");
    write(unsorted / "hotbuttons.ini", "buttons");
    fs::permissions(unsorted / "hotbuttons.ini", fs::perms::none);

    const CharacterFiles alpha = character_files("p", "Alpha", "Bob", servers);
    CHECK(adopt_unsorted_character_files("p", "Alpha", "Bob", servers) == 0);
    CHECK_FALSE(fs::exists(alpha.dir));
    CHECK(snapshot(servers / "p" / "characters" / "Alpha").empty()); // nor a half-made sibling

    fs::permissions(unsorted / "hotbuttons.ini", fs::perms::owner_read | fs::perms::owner_write);
    CHECK(adopt_unsorted_character_files("p", "Alpha", "Bob", servers) == 2);
    CHECK(snapshot(alpha.dir) ==
          std::map<std::string, std::string>{{"UI.ini", "windows"}, {"hotbuttons.ini", "buttons"}});
}
#endif

TEST_CASE("character files: /outputfile may not take the names of the files kept in the folder") {
    // Every file CharacterFiles names, in any case, and the UI packs' layouts.
    CharacterFiles f{std::filesystem::path("c")};
    for (const auto& p : {f.ui_ini(), f.hot_buttons(), f.trader_prices(), f.barter_lines(), f.barter_log(),
                          f.address_book(), f.ignore_list(), f.guild_notes(), f.chat_ini(), f.options_ini(),
                          f.chat_history(), f.scripts_ini(), f.kept_items(), f.settings(), f.ui_settings("modern")}) {
        CAPTURE(p.filename().string());
        CHECK(is_kept_character_file(p.filename().string()));
    }
    CHECK(is_kept_character_file("SCRIPTS.INI"));
    CHECK(is_kept_character_file("Hailstone.ini"));
    CHECK(is_kept_character_file("UI-Gamepad.TXT"));
    CHECK(is_kept_character_file("plugin-config"));
    // The writers' default names and anything else are written.
    CHECK_FALSE(is_kept_character_file("Bob-Inventory.txt"));
    CHECK_FALSE(is_kept_character_file("RaidRoster-20261109-1200.txt"));
    CHECK_FALSE(is_kept_character_file("scripts.ini.txt"));
    CHECK_FALSE(is_kept_character_file("ui.txt"));
    CHECK_FALSE(is_kept_character_file("eqlog.txt")); // a chat log, as the original's fopen would replace
}

