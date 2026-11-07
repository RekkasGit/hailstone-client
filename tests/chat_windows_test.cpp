// The chat windows over the eq API (app/lua_eq_chat.cpp): new windows, per-window channels, default channel,
// clear, close, and the settings kept in the character's UI ini.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>

#include <imgui.h>
#include <lua.hpp>

#include "app/lua_eq.h"
#include "game/chat_windows.h"
#include "game/social.h"
#include "ui/ui_ini.h"
#include "core/game_dir.h"
#include "net/game_connection.h"
#include "platform/local_time.h"
#include "ui/dev_screens.h"
#include "scratch_dir.h"

using namespace nmsc;
namespace fs = std::filesystem;

namespace {

struct Lua {
    lua_State* L = luaL_newstate();
    explicit Lua(app::EqApi& api) {
        luaL_openlibs(L);
        lua_newtable(L);
        api.register_functions(L);
        lua_setglobal(L, "eq");
    }
    ~Lua() { lua_close(L); }
    // Runs `code` (which returns one value) and gives it as text.
    std::string run(const std::string& code) {
        if (luaL_dostring(L, code.c_str()) != LUA_OK) {
            std::string e = lua_tostring(L, -1);
            lua_pop(L, 1);
            return "error: " + e;
        }
        std::string out = lua_isnil(L, -1) ? "nil" : luaL_tolstring(L, -1, nullptr);
        lua_settop(L, 0);
        return out;
    }
};

// The texts of a window's lines, joined with '|' (the STML tags stripped).
const char* kLines = R"(
local function lines(slot)
  local _, l = eq.ChatText(slot)
  local out = {}
  for _, s in ipairs(l or {}) do out[#out + 1] = (s:gsub("<[^>]*>", "")) end
  return table.concat(out, "|")
end
)";

} // namespace

// The eqstr texts the chat window manager's menus and answers use (as in eqstr_us.txt).
void write_strings(const fs::path& game) {
    std::ofstream(game / "eqstr_us.txt") << "EQST0002\n"
                                          << "6623 Main Chat\n6624 Chat\n6630 Say\n6631 Tell\n6632 Group\n"
                                          << "6633 Guild\n6634 OOC\n6635 Auction\n6636 Shout\n6637 Emotes\n"
                                          << "5113 Raid\n6638 Chat Channel\n6639 Melee\n6640 Spells\n6645 Filters\n"
                                          << "3135 will go to this window.\n3136 will go to the main window.\n"
                                          << "3138  is now the default channel for this window.\n"
                                          << "6041 All melee channels will now filter to this window.\n"
                                          << "15014 Your Hits\n15026 My Death\n15034 Normal\n15035 Abbreviated\n"
                                          << "13205 ALL\n6300 Bad Word\n6306 Guild Chat\n6301 PC Spells\n";
}

TEST_CASE("Chat windows: lines go to the window of their colour's channel (ChannelMap), kept in the UI ini") {
    ImGuiContext* ctx = ImGui::CreateContext();
    nmsc::test::ScratchDir dir_scratch("chat_windows_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "game");
    write_strings(dir / "game");
    GameDir game(dir / "game");
    std::vector<std::string> submitted;
    {
        net::GameConnection conn(game);
        ui::ChatView view;
        app::EqHost host;
        host.in_game = host.ui_ready = true;
        host.game = &game;
        host.conn = &conn;
        host.chat = &view;
        host.submit_chat = [&](const std::string& t) { submitted.push_back(t); };
        app::EqApi api(host);
        api.load_layout(dir / "defaults.ini", dir / "UI_Test_Server.ini", {});
        Lua lua(api);
        lua.run(kLines + std::string("_G.lines = lines"));

        view.add(ui::ChatChannel::Say, "hello");
        view.add(ui::ChatChannel::Tell, "psst");
        view.add(ui::ChatChannel::System, "You have been slain", 0, 277); // My Death (channel 28)
        CHECK(lua.run("return #eq.ChatWindows()") == "1");
        CHECK(lua.run("return eq.ChatWindows()[1].title") == "Main Chat");
        CHECK(lua.run("return lines(1)") == "hello|psst|You have been slain"); // every channel starts in the main one

        // A new window is a stock window that shows (eq.ReplaceWindow), empty, with no channels.
        CHECK(lua.run("return tostring(eq.ReplaceWindow('Chat2'))") == "false");
        CHECK(lua.run("return eq.ChatNewWindow()") == "2");
        CHECK(lua.run("return eq.ChatWindows()[2].title") == "Chat 1");
        CHECK(lua.run("return tostring(eq.ReplaceWindow('Chat2'))") == "true");
        CHECK(lua.run("return lines(2)") == "");
        // Melee's ALL: its 18 channels come here, and the window says so; lines already shown stay where they are.
        lua.run("eq.ChatSetFilter(2, 'Melee', true)");
        CHECK(lua.run("return lines(2)") == "All melee channels will now filter to this window.");
        CHECK(lua.run("local n = 0; for _, f in ipairs(eq.ChatFilters(2)) do if f.on then n = n + 1 end end; return n") == "18");
        view.add(ui::ChatChannel::System, "You have been slain", 0, 277);
        view.add(ui::ChatChannel::Combat, "You hit a rat for 3 points of damage.", 0, 265); // Your Hits (9)
        CHECK(lua.run("return lines(2)") ==
              "All melee channels will now filter to this window.|You have been slain|You hit a rat for 3 points of damage.");
        CHECK(lua.run("return lines(1)") == "hello|psst|You have been slain");
        // One channel back to the main window (its item a second time).
        lua.run("eq.ChatSetFilter(2, 28, false)");
        CHECK(lua.run("local _, l = eq.ChatText(2); return #l") == "4"); // "My Death will go to the main window."
        view.add(ui::ChatChannel::System, "You died.", 0, 277);
        CHECK(lua.run("return lines(1)") == "hello|psst|You have been slain|You died.");

        // A tab in the main window: a chat window of its own (Say comes to it).
        CHECK(lua.run("return eq.ChatNewTab(1, 'Social')") == "2");
        lua.run("eq.ChatSetFilter(1, 0, true)");
        CHECK(lua.run("local w = eq.ChatWindows()[1]; return w.tab .. #w.tabs .. w.title") == "22Social");
        view.add(ui::ChatChannel::Say, "news");
        CHECK(lua.run("return lines(1)") == "Say will go to this window.|news");
        view.add(ui::ChatChannel::Tell, "psst again");
        CHECK(lua.run("return tostring(eq.ChatWindows()[1].tabs[1].unread)") == "true"); // the tab not up
        lua.run("eq.ChatSelectTab(1, 1)");
        CHECK(lua.run("return lines(1)") == "hello|psst|You have been slain|You died.|psst again");

        // Plain text goes to the window's channel (the Channel menu); commands as typed.
        lua.run("eq.ChatSetDefault(2, 2)"); // Group
        lua.run("eq.ChatSubmit(2, 'inc')");
        lua.run("eq.ChatSubmit(2, '/who')");
        lua.run("eq.ChatSubmit(1, 'hi')");
        CHECK(submitted == std::vector<std::string>{"/gsay inc", "/who", "hi"});
        CHECK(lua.run("local _, active, d = eq.ChatInfo(1); return tostring(active) .. d") == "trueSay");
        lua.run("eq.ChatSetDefault(2, 1)"); // Tell: disabled in the menu
        CHECK(lua.run("local _, _, d = eq.ChatInfo(2); return d") == "Group");

        // Clear empties one window only.
        lua.run("eq.ChatClear(2)");
        CHECK(lua.run("return lines(2)") == "");
        CHECK(lua.run("return lines(1)") != "");

        // Tell windows: a tell goes to its player's tell window, typed text back to that player.
        lua.run("eq.ChatSetTellWindows(true)");
        view.add(ui::ChatChannel::Tell, "Bob tells you, 'hi'", 0, 257, "Bob");
        CHECK(lua.run("local w = eq.ChatWindows(); return #w .. tostring(w[3].tells) .. w[3].title") == "3trueBob");
        CHECK(lua.run("return lines(eq.ChatWindows()[3].slot)") == "Bob tells you, 'hi'");
        CHECK(lua.run("return eq.ChatWindows()[3].tellsIn") == "1"); // (the modern tell window flashes)
        view.add(ui::ChatChannel::Tell, "You told Ann, 'yo'", 0, 308, "Ann");
        CHECK(lua.run("return #eq.ChatWindows()[3].tabs") == "2"); // Ann's is a tab of Bob's
        CHECK(lua.run("return eq.ChatWindows()[3].tellsIn") == "1"); // our own tell's echo is not one
        view.add(ui::ChatChannel::Tell, "Ann tells you, 'ok'", 0, 257, "Ann");
        CHECK(lua.run("return eq.ChatWindows()[3].tellsIn") == "2"); // Ann's tab counts for the window
        // Timestamps: "[hh:mm:ss] " before each line, another version than without.
        CHECK(lua.run("local s = eq.ChatWindows()[3].slot; local _, l = eq.ChatText(s, nil, false, true) "
                      "return (l[1]:gsub('<[^>]*>', '')):match('^%[%d%d:%d%d:%d%d%] Bob tells you') ~= nil") == "true");
        CHECK(lua.run("local s = eq.ChatWindows()[3].slot; return eq.ChatText(s) ~= eq.ChatText(s, nil, false, true)") ==
              "true");
        // A stamped window that holds the current version gets only the version back, as an unstamped one does.
        CHECK(lua.run("local s = eq.ChatWindows()[3].slot; local v = eq.ChatText(s, nil, false, true) "
                      "local v2, l = eq.ChatText(s, v, false, true) return tostring(v2 == v) .. tostring(l)") ==
              "truenil");
        // The stamp is the line's local time, worked out once a minute: seconds within a minute, the next minute.
        for (std::int64_t t : {std::int64_t{1700000040}, std::int64_t{1700000041}, std::int64_t{1700000099},
                               std::int64_t{1700000100}, std::int64_t{1700003640}}) {
            for (auto& l : view.lines)
                if (l.peer == "Bob") l.time = t;
            auto tm = platform::local_time(static_cast<std::time_t>(t));
            REQUIRE(tm);
            CHECK(lua.run("local s = eq.ChatWindows()[3].slot; local _, l = eq.ChatText(s, nil, false, true) "
                          "return (l[1]:gsub('<[^>]*>', '')):sub(1, 10)") ==
                  std::format("[{:02d}:{:02d}:{:02d}]", tm->tm_hour, tm->tm_min, tm->tm_sec));
        }
        CHECK(lua.run("return eq.ChatWindows()[3].title") == "Bob");
        lua.run("local s = eq.ChatWindows()[3].slot; eq.ChatSubmit(s, 'back')");
        CHECK(submitted.back() == "/tell Bob back");

        // Hit modes: 0..2, ALL sets them all.
        lua.run("eq.ChatSetHitMode(1, 1)");
        CHECK(conn.hit_modes()[0] == 1);
        lua.run("eq.ChatSetHitMode(7, 2)");
        CHECK(conn.hit_modes()[5] == 2);

        // The Options window's chat page lists the filter table's 33 rows (the original's order and names).
        auto filters = api.list_texts("OptionsWindow", "FilterList");
        REQUIRE(filters.size() == 33);
        CHECK(filters[0] == "Bad Word");
        CHECK(filters[1] == "Guild Chat");
        CHECK(filters[7] == "PC Spells");

        lua.run("eq.ChatSetFont(2, 5); eq.ChatSetTitle(2, 'Fights')");
        api.save_layout();
        // the original's keys
        ui::UiIni* ini = app::chat_ui_ini();
        REQUIRE(ini);
        CHECK(ini->get("ChatManager", "NumWindows") == "2");
        CHECK(ini->get("ChatManager", "ChatWindow1_Name") == "Fights");
        CHECK(ini->get("ChatManager", "ChatWindow1_DefaultChannel") == "2");
        CHECK(ini->get("ChatManager", "ChatWindow1_FontStyle") == "5");
        CHECK(ini->get("ChatManager", "ChannelMap9") == "1");
        CHECK(ini->get("ChatManager", "ChannelMap28") == "0");
        CHECK(ini->get("ChatManager", "ChannelMap0") == "2");
        CHECK(ini->get("ChatManager", "HitMode0") == "2");
        // /channel on a chat server channel (Cmd_channel: ChatChannel 0x14, its name at +0x234 and its index at
        // +0x230) is kept too: the exe's "%s_ChatChannel" / "%s_TellTarget" / "%s_DefaultChannel" keys
        lua.run("eq.ChatSetAlwaysHere(1, false)"); // (the main window's lock first: the last-clicked window)
        lua.run("eq.ChatActivate(2)");
        REQUIRE(app::set_active_chat_default(0x14, "nmsguild", 1));
        api.save_layout();
        CHECK(ini->get("ChatManager", "ChatWindow1_DefaultChannel") == "20");
        CHECK(ini->get("ChatManager", "ChatWindow1_ChatChannel") == "1");
        CHECK(ini->get("ChatManager", "ChatWindow1_TellTarget") == "nmsguild");
    }
    {
        // The next session reads the windows back (not the tell windows).
        net::GameConnection conn(game);
        ui::ChatView view;
        app::EqHost host;
        host.in_game = host.ui_ready = true;
        host.game = &game;
        host.conn = &conn;
        host.chat = &view;
        app::EqApi api(host);
        api.load_layout(dir / "defaults.ini", dir / "UI_Test_Server.ini", {});
        Lua lua(api);
        lua.run(kLines + std::string("_G.lines = lines"));
        CHECK(lua.run("local w = eq.ChatWindows(); return #w .. w[2].title .. #w[1].tabs") == "2Fights2");
        CHECK(lua.run("return (eq.ChatInfo(2))") == "5");
        CHECK(lua.run("return (select(3, eq.ChatInfo(2)))") == "nmsguild"); // the chat channel default, read back
        CHECK(lua.run("return tostring(eq.ChatTellWindows())") == "true");
        CHECK(conn.hit_modes()[0] == 2);
        view.add(ui::ChatChannel::System, "a rat hits YOU for 3 points of damage.", 0, 266); // You Being Hit (24)
        view.add(ui::ChatChannel::Say, "chatter");
        view.add(ui::ChatChannel::OOC, "ooc");
        CHECK(lua.run("return lines(2)") == "a rat hits YOU for 3 points of damage.");
        lua.run("eq.ChatSelectTab(1, 2)");
        CHECK(lua.run("return lines(1)") == "chatter");
        lua.run("eq.ChatSelectTab(1, 1)");
        CHECK(lua.run("return lines(1)") == "ooc");
        // Closing a window sends its channels back to the main window; the main window stays.
        lua.run("eq.ChatCloseWindow(2)");
        CHECK(lua.run("return #eq.ChatWindows()") == "1");
        view.add(ui::ChatChannel::System, "a rat hits YOU for 4 points of damage.", 0, 266);
        CHECK(lua.run("return lines(1)") == "ooc|a rat hits YOU for 4 points of damage.");
        lua.run("eq.ChatCloseTab(1, 2); eq.ChatCloseWindow(1)");
        CHECK(lua.run("return #eq.ChatWindows()[1].tabs") == "1");
    }
    ImGui::DestroyContext(ctx);
    fs::remove_all(dir);
}

TEST_CASE("Chat windows: each window keeps its own last 1000 lines; a busy one does not push out a quiet one's") {
    ImGuiContext* ctx = ImGui::CreateContext();
    nmsc::test::ScratchDir dir_scratch("chat_window_lines_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "game");
    write_strings(dir / "game");
    GameDir game(dir / "game");
    {
        net::GameConnection conn(game);
        ui::ChatView view;
        app::EqHost host;
        host.in_game = host.ui_ready = true;
        host.game = &game;
        host.conn = &conn;
        host.chat = &view;
        app::EqApi api(host);
        api.load_layout(dir / "defaults.ini", dir / "UI_Test_Server.ini", {});
        Lua lua(api);
        lua.run(kLines + std::string("_G.lines = lines"));
        const char* count2 = "local _, l = eq.ChatText(2) return #l";
        const char* first2 = "local _, l = eq.ChatText(2) return (l[1]:gsub('<[^>]*>', ''))";

        view.add(ui::ChatChannel::Tell, "psst");
        view.add(ui::ChatChannel::Say, "hello");
        CHECK(lua.run("return eq.ChatNewWindow()") == "2");
        lua.run("eq.ChatSetFilter(2, 'Melee', true)"); // (and its line "All melee channels ...")
        for (int i = 1; i <= 3500; ++i) { // (more than the whole buffer once held)
            view.add(ui::ChatChannel::Combat, std::format("hit {}", i), 0, 265); // Your Hits: window 2
            if (i % 100 == 0) lua.run("return #eq.ChatWindows()"); // routed (and trimmed) as the scripts look
        }
        CHECK(lua.run(count2) == "1000");
        CHECK(lua.run(first2) == "hit 2501");
        CHECK(lua.run("return lines(1)") == "psst|hello"); // the main window's lines all stay
        CHECK(view.lines.size() == 1002);

        // Clearing still goes by each line's number, with lines gone from the middle of the buffer.
        lua.run("eq.ChatClear(1)");
        CHECK(lua.run("return lines(1)") == "");
        view.add(ui::ChatChannel::Say, "again");
        CHECK(lua.run("return lines(1)") == "again");
        CHECK(lua.run(count2) == "1000");
        view.add(ui::ChatChannel::Combat, "hit 3501", 0, 265);
        CHECK(lua.run(first2) == "hit 2502");
    }
    ImGui::DestroyContext(ctx);
}

TEST_CASE("Chat windows: eq.ChatText's version is the window's own; the incremental form sends only the new lines") {
    ImGuiContext* ctx = ImGui::CreateContext();
    nmsc::test::ScratchDir dir_scratch("chat_text_since_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "game");
    write_strings(dir / "game");
    GameDir game(dir / "game");
    {
        net::GameConnection conn(game);
        ui::ChatView view;
        app::EqHost host;
        host.in_game = host.ui_ready = true;
        host.game = &game;
        host.conn = &conn;
        host.chat = &view;
        app::EqApi api(host);
        api.load_layout(dir / "defaults.ini", dir / "UI_Test_Server.ini", {});
        Lua lua(api);
        // A script's copy of a window's lines, kept up with the incremental form as the modern chat keeps it.
        lua.run(R"(
_G.held = {}
function _G.follow(slot)
  local h = held[slot] or { version = -1, lines = {} }
  held[slot] = h
  local v, l, reset, count = eq.ChatText(slot, h.version, false, false, true)
  if v == h.version then return "same" end
  h.version = v
  if reset then h.lines = {} end
  for _, s in ipairs(l) do h.lines[#h.lines + 1] = (s:gsub("<[^>]*>", "")) end
  while #h.lines > count do table.remove(h.lines, 1) end
  return (reset and "reset:" or "add:") .. #l
end
function _G.text(slot) return table.concat(held[slot].lines, "|") end
function _G.all(slot)
  local _, l = eq.ChatText(slot)
  local out = {}
  for _, s in ipairs(l) do out[#out + 1] = (s:gsub("<[^>]*>", "")) end
  return table.concat(out, "|")
end
)");
        view.add(ui::ChatChannel::Say, "hello");
        CHECK(lua.run("return eq.ChatNewWindow()") == "2");
        lua.run("eq.ChatSetFilter(2, 'Melee', true)");
        CHECK(lua.run("return follow(1)") == "reset:1");
        CHECK(lua.run("return follow(2)") == "reset:1"); // (its "All melee channels ..." line)
        CHECK(lua.run("return follow(1)") == "same");

        // A line in one window leaves the other's version as it was; the window it went to gets that line alone.
        CHECK(lua.run("_G.v1 = eq.ChatText(1) return 'ok'") == "ok");
        view.add(ui::ChatChannel::Combat, "hit 1", 0, 265);
        CHECK(lua.run("return tostring(eq.ChatText(1) == v1)") == "true");
        CHECK(lua.run("return follow(1)") == "same");
        CHECK(lua.run("return follow(2)") == "add:1");
        view.add(ui::ChatChannel::Say, "again");
        view.add(ui::ChatChannel::Say, "and again");
        CHECK(lua.run("return follow(1)") == "add:2");
        CHECK(lua.run("return text(1)") == "hello|again|and again");
        CHECK(lua.run("return text(1) == all(1)") == "true");
        // The old form still gives every line when the version differs, and the version alone when it is the same.
        CHECK(lua.run("local v, l = eq.ChatText(1, -1) return #l") == "3");
        CHECK(lua.run("local v = eq.ChatText(1) local v2, l = eq.ChatText(1, v) return tostring(l)") == "nil");

        // Past 1000 lines the oldest leave the front: the script drops them by the count.
        for (int i = 2; i <= 1003; ++i) view.add(ui::ChatChannel::Combat, std::format("hit {}", i), 0, 265);
        CHECK(lua.run("return follow(2)") == "add:1000"); // (hit 2 and 3 left the buffer before the script looked)
        CHECK(lua.run("return #held[2].lines .. ' ' .. held[2].lines[1]") == "1000 hit 4");
        CHECK(lua.run("return text(2) == all(2)") == "true");

        // Clear: everything again (none), and from there new lines only.
        lua.run("eq.ChatClear(1)");
        CHECK(lua.run("return follow(1)") == "reset:0");
        view.add(ui::ChatChannel::Say, "after");
        CHECK(lua.run("return follow(1)") == "add:1");
        CHECK(lua.run("return text(1)") == "after");
        // Another stamp setting, another window's version: everything again.
        CHECK(lua.run("local v, l, reset = eq.ChatText(1, held[1].version, false, true, true) return tostring(reset)") ==
              "true");
        CHECK(lua.run("local v, l, reset = eq.ChatText(2, held[1].version, false, false, true) return tostring(reset)") ==
              "true");

        // The buffer emptied (a camp): with no line since, everything again (none); with lines since, those, and
        // the lines gone are dropped by the count.
        view.lines.clear();
        ++view.revision;
        CHECK(lua.run("return follow(2)") == "reset:0");
        CHECK(lua.run("return text(2)") == "");
        CHECK(lua.run("return follow(1)") == "reset:0");
        view.add(ui::ChatChannel::Say, "new");
        CHECK(lua.run("return follow(1)") == "add:1");
        CHECK(lua.run("return text(1)") == "new");
        view.lines.clear();
        ++view.revision;
        view.add(ui::ChatChannel::Say, "fresh");
        CHECK(lua.run("return follow(1)") == "add:1");
        CHECK(lua.run("return text(1)") == "fresh");
    }
    ImGui::DestroyContext(ctx);
}

TEST_CASE("Chat windows: GetChannelFromColor, the Filters menu, the tell menus' text, LastTell_Add") {
    CHECK(game::chat_channel_of_color(256) == 0);  // say
    CHECK(game::chat_channel_of_color(307) == 0);  // our say
    CHECK(game::chat_channel_of_color(257) == 1);  // tell
    CHECK(game::chat_channel_of_color(308) == 1);  // the tell echo
    CHECK(game::chat_channel_of_color(327) == 3);  // raid
    CHECK(game::chat_channel_of_color(345) == 4);  // guild
    CHECK(game::chat_channel_of_color(291) == 12); // chat channel 1
    CHECK(game::chat_channel_of_color(324) == 21); // our chat channel 10
    CHECK(game::chat_channel_of_color(277) == 28); // my death
    CHECK(game::chat_channel_of_color(350) == 56); // PvP (0x15e)
    CHECK(game::chat_channel_of_color(13) == 22);  // the basic colours: Other
    CHECK(game::chat_channel_of_color(351) == 22);
    // every channel but the unused 48 is in a menu once
    std::array<int, game::kChatChannelCount> seen{};
    for (const auto& it : game::chat_filter_items()) ++seen[it.channel];
    for (int ch = 0; ch < game::kChatChannelCount; ++ch) CHECK(seen[static_cast<std::size_t>(ch)] == (ch == 48 ? 0 : 1));

    CHECK(game::tell_menu_text("Bob", "tell") == "/tell Bob ");
    CHECK(game::tell_menu_text("server.Bob", "tell") == ";tell server.Bob ");
    CHECK(game::tell_menu_text(".Bob", "tell") == ";tell Bob ");
    CHECK(game::tell_menu_text("[Bob", "tell") == "[tell Bob ");

    std::vector<std::string> names;
    for (int i = 0; i < 55; ++i) game::add_last_tell(names, "p" + std::to_string(i));
    CHECK(names.size() == 50);
    CHECK(names.front() == "p54");
    game::add_last_tell(names, "P50");
    CHECK(names.size() == 50);
    CHECK(names[0] == "P50");
    CHECK(names[1] == "p54");
    CHECK(std::count_if(names.begin(), names.end(), [](const std::string& n) { return n == "p50"; }) == 0);

    CHECK(game::hit_mode_eqstr(0, 2) == 15036); // Number Only
    CHECK(game::hit_mode_eqstr(1, 2) == 15037); // Miss Only
    CHECK(game::hit_mode_eqstr(6, 2) == 15036); // ALL's
}

TEST_CASE("Chat windows: the reply keys' index (LastTell_CycleIndex) and CYCLEREPLY's text") {
    std::vector<std::string> names;
    int index = 0;
    CHECK_FALSE(game::cycle_last_tell(names, index, false)); // no names: 12364, the index stays
    CHECK(index == 0);
    names = {"Cara", "Bob", "Ann"}; // the newest first
    // CYCLEREPLY: one older while there is one, then back to the newest
    CHECK(game::cycle_last_tell(names, index, false));
    CHECK(index == 1);
    game::cycle_last_tell(names, index, false);
    CHECK(index == 2);
    game::cycle_last_tell(names, index, false);
    CHECK(index == 0);
    // CYCLEREPLY_BACK: one newer, from the newest round to the oldest
    game::cycle_last_tell(names, index, true);
    CHECK(index == 2);
    game::cycle_last_tell(names, index, true);
    CHECK(index == 1);
    std::vector<std::string> full;
    for (int i = 0; i < 50; ++i) full.push_back("p" + std::to_string(i));
    index = 0;
    game::cycle_last_tell(full, index, true);
    CHECK(index == 49);
    game::cycle_last_tell(full, index, false);
    CHECK(index == 0); // past the 50th: the newest

    CHECK(game::reply_target_text("Bob", "/rtarget") == "/rtarget Bob");
    CHECK(game::reply_target_text("[Bob", "/rtarget") == "/rtarget Bob");
    CHECK(game::reply_target_text(";Bob", "/rtarget") == "/rtarget Bob");

    // The chat input's text decides: past its first character "rtarget", else "tell" (case-sensitive), else nothing.
    index = 0;
    auto r = game::cycle_reply("/tell ", names, index, false, "/tell", "/rtarget");
    CHECK(index == 1);
    CHECK(r.text == "/tell Bob ");
    CHECK(r.messages.empty());
    r = game::cycle_reply(";tell Bob hi", names, index, false, "/tell", "/rtarget");
    CHECK(r.text == "/tell Ann ");
    r = game::cycle_reply("/rtarget Ann", names, index, true, "/tell", "/rtarget");
    CHECK(index == 1);
    CHECK(r.text == "/rtarget Bob");
    names[1] = "[Bob";
    r = game::cycle_reply("/tell x", names, index, true, "/tell", "/rtarget");
    CHECK(r.text == "/tell Cara ");
    r = game::cycle_reply("/tell x", names, index, false, "/tell", "/rtarget");
    CHECK(r.text == "[tell Bob ");
    r = game::cycle_reply("/TELL x", names, index, false, "/tell", "/rtarget");
    CHECK(r.text.empty());
    CHECK(index == 1); // not a reply line: the index stays
    r = game::cycle_reply("hello", names, index, false, "/tell", "/rtarget");
    CHECK(r.text.empty());
    CHECK(r.messages.empty());
    // Nobody to reply to: 12364 (the index's) and 12365, the input stays.
    names.clear();
    index = 0;
    r = game::cycle_reply("/tell ", names, index, false, "/tell", "/rtarget");
    CHECK(r.text.empty());
    CHECK(r.messages == std::vector<std::uint32_t>{12364, 12365});

    // Each new name puts the index back on the newest.
    game::SocialState social;
    game::add_last_tell(social, "Ann");
    game::add_last_tell(social, "Bob");
    social.last_tell_index = 1;
    game::add_last_tell(social, "Cara");
    CHECK(social.last_tell_index == 0);
    CHECK(social.last_tells.front() == "Cara");
}

TEST_CASE("Chat windows: the chat keys fill the active window's input (TELL, CMDMODE_*, HISTORY_*) and page it") {
    ImGuiContext* ctx = ImGui::CreateContext();
    nmsc::test::ScratchDir dir_scratch("chat_keys_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "game");
    write_strings(dir / "game");
    GameDir game(dir / "game");
    std::vector<std::string> submitted;
    {
        net::GameConnection conn(game);
        ui::ChatView view;
        app::EqHost host;
        host.in_game = host.ui_ready = true;
        host.game = &game;
        host.conn = &conn;
        host.chat = &view;
        host.submit_chat = [&](const std::string& t) { submitted.push_back(t); };
        app::EqApi api(host);
        api.load_layout(dir / "defaults.ini", dir / "UI_Test_Server.ini", {});
        Lua lua(api);
        const char* take = "local t, r = eq.ChatTakeFocus(1, 'typed'); return tostring(t) .. '|' .. tostring(r)";
        CHECK(lua.run(take) == "nil|nil");
        app::tell_key(); // (no eqstr 13974 here: the English word)
        CHECK(lua.run(take) == "/tell |true");
        CHECK(lua.run(take) == "nil|nil"); // once
        app::put_chat_input("'");
        CHECK(lua.run(take) == "'|true");

        // HISTORY_UP / HISTORY_DOWN: the lines sent before, as the input's Up / Down
        lua.run("eq.ChatSubmit(1, 'one')");
        lua.run("eq.ChatSubmit(1, 'two')");
        app::chat_history_key(true);
        CHECK(lua.run(take) == "two|true");
        app::chat_history_key(true);
        CHECK(lua.run(take) == "one|true");
        app::chat_history_key(false);
        CHECK(lua.run(take) == "two|true");

        // PAGEUP / PAGEDOWN: once, for the active window
        CHECK(lua.run("return eq.ChatPage(1)") == "nil");
        CHECK(lua.run("return eq.ChatNewWindow()") == "2");
        app::chat_page_key(-1);
        CHECK(lua.run("return eq.ChatPage(2)") == "nil");
        CHECK(lua.run("return eq.ChatPage(1)") == "-1");
        CHECK(lua.run("return eq.ChatPage(1)") == "nil");
        lua.run("eq.ChatSetAlwaysHere(1, false)");
        lua.run("eq.ChatActivate(2)");
        app::chat_page_key(1);
        CHECK(lua.run("return eq.ChatPage(2)") == "1");

        // The chat input's Tab with nobody to reply to, or on another line: the input stays.
        CHECK(lua.run("return eq.ChatCycleReply('/tell ', false)") == "nil");
        CHECK(lua.run("return eq.ChatCycleReply('hello', true)") == "nil");

        // ':' and "'" lines are the emote and say commands whatever the window's channel (CMDMODE_EMOTE / _SAY).
        lua.run("eq.ChatSetDefault(1, 2)"); // Group
        lua.run("eq.ChatSubmit(1, ':waves')");
        lua.run("eq.ChatSubmit(1, \"'hi\")");
        lua.run("eq.ChatSubmit(1, 'inc')");
        REQUIRE(submitted.size() >= 3);
        CHECK(std::vector<std::string>(submitted.end() - 3, submitted.end()) ==
              std::vector<std::string>{":waves", "'hi", "/gsay inc"});
    }
    ImGui::DestroyContext(ctx);
}

TEST_CASE("Chat windows: Always Chat Here takes the chat keys; the main window has it by default") {
    ImGuiContext* ctx = ImGui::CreateContext();
    nmsc::test::ScratchDir dir_scratch("chat_here_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "game");
    write_strings(dir / "game");
    GameDir game(dir / "game");
    auto takes = [](Lua& lua, int slot) {
        return lua.run(std::format("local t = eq.ChatTakeFocus({}); return tostring(t)", slot));
    };
    const char* here = "local s = '' for i = 1, 3 do s = s .. tostring(select(5, eq.ChatInfo(i))):sub(1, 1) end return s";
    {
        net::GameConnection conn(game);
        ui::ChatView view;
        app::EqHost host;
        host.in_game = host.ui_ready = true;
        host.game = &game;
        host.conn = &conn;
        host.chat = &view;
        app::EqApi api(host);
        api.load_layout(dir / "defaults.ini", dir / "UI_Test_Server.ini", {});
        Lua lua(api);
        lua.run(kLines + std::string("_G.lines = lines"));
        CHECK(lua.run("return eq.ChatNewWindow()") == "2");
        CHECK(lua.run("return eq.ChatNewWindow()") == "3");
        CHECK(lua.run(here) == "tff"); // a new character: the main window's
        // Another window clicked: the keys still start a line in the main window.
        lua.run("eq.ChatActivate(2)");
        app::tell_key();
        CHECK(takes(lua, 2) == "nil");
        CHECK(takes(lua, 1) == "/tell ");
        CHECK(lua.run("return tostring(select(2, eq.ChatInfo(1)))") == "true");

        // Ticked on window 3: it takes the keys (and says so in itself).
        std::string before = lua.run("return lines(3)");
        lua.run("eq.ChatSetAlwaysHere(3, true)");
        CHECK(lua.run(here) == "fft");
        CHECK(lua.run("return lines(3)") != before);
        app::tell_key();
        CHECK(takes(lua, 1) == "nil");
        CHECK(takes(lua, 3) == "/tell ");
        api.save_layout();
        ui::UiIni* ini = app::chat_ui_ini();
        REQUIRE(ini);
        CHECK(ini->get("ChatManager", "LockedActiveWindow") == "2");

        // Unticked there: back to the main window.
        lua.run("eq.ChatSetAlwaysHere(3, false)");
        CHECK(lua.run(here) == "tff");
        // Window 2 locked and closed: the main window again.
        lua.run("eq.ChatSetAlwaysHere(2, true)");
        lua.run("eq.ChatCloseWindow(2)");
        CHECK(lua.run("return tostring(select(5, eq.ChatInfo(1)))") == "true");
        // Unticked on the main window: no lock, the last-clicked window has the keys (the original's way).
        lua.run("eq.ChatSetAlwaysHere(1, false)");
        CHECK(lua.run("return tostring(select(5, eq.ChatInfo(1)))") == "false");
        lua.run("eq.ChatActivate(3)");
        app::tell_key();
        CHECK(takes(lua, 3) == "/tell ");
        api.save_layout();
        CHECK(ini->get("ChatManager", "LockedActiveWindow") == "-1");
    }
    {
        // The next session keeps "no lock".
        net::GameConnection conn(game);
        ui::ChatView view;
        app::EqHost host;
        host.in_game = host.ui_ready = true;
        host.game = &game;
        host.conn = &conn;
        host.chat = &view;
        app::EqApi api(host);
        api.load_layout(dir / "defaults.ini", dir / "UI_Test_Server.ini", {});
        Lua lua(api);
        CHECK(lua.run("return tostring(select(5, eq.ChatInfo(1)))") == "false");
    }
    ImGui::DestroyContext(ctx);
}

TEST_CASE("Chat windows: the chat history keeps each line's time; a version 1 file reads without times") {
    ImGuiContext* ctx = ImGui::CreateContext();
    nmsc::test::ScratchDir dir_scratch("chat_history_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "game");
    write_strings(dir / "game");
    GameDir game(dir / "game");
    // One session: its lines in a fresh chat view, `check` run once the layout (and the history) is loaded.
    auto session = [&](const std::function<void(Lua&, ui::ChatView&)>& check) {
        net::GameConnection conn(game);
        ui::ChatView view;
        app::EqHost host;
        host.in_game = host.ui_ready = true;
        host.game = &game;
        host.conn = &conn;
        host.chat = &view;
        app::EqApi api(host);
        api.load_layout(dir / "defaults.ini", dir / "UI_Test_Server.ini", {});
        Lua lua(api);
        check(lua, view);
        app::save_chat_history(true);
    };
    const char* stamped = "local _, l = eq.ChatText(1, nil, false, true) local n = 0 "
                          "for _, s in ipairs(l) do if s:gsub('<[^>]*>', ''):match('^%[%d%d:%d%d:%d%d%] ') then n = n + 1 end end "
                          "return n .. '/' .. #l";
    session([&](Lua& lua, ui::ChatView& view) {
        view.add(ui::ChatChannel::Say, "hello");
        view.add(ui::ChatChannel::OOC, "ooc");
        CHECK(lua.run(stamped) == "2/2");
    });
    // The next session: both lines back with their times; the session mark has none.
    session([&](Lua& lua, ui::ChatView&) { CHECK(lua.run(stamped) == "2/3"); });
    // A version 1 file (no time column): its line comes back without a stamp.
    fs::path file;
    for (const auto& e : fs::recursive_directory_iterator(dir))
        if (e.path().filename() == "chat_history.txt") file = e.path();
    REQUIRE_FALSE(file.empty());
    std::ofstream(file) << "# Hailstone chat history 1\n" << "1\t0\t0\t\told line\n";
    session([&](Lua& lua, ui::ChatView&) {
        CHECK(lua.run(stamped) == "0/2");
        CHECK(lua.run("local _, l = eq.ChatText(1) return (l[1]:gsub('<[^>]*>', ''))") == "old line");
    });
    ImGui::DestroyContext(ctx);
}
