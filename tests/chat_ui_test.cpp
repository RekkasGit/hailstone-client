// The chat window script (ui/modern/chat.lua, with the rest of the Modern UI) run for real over Dear ImGui with no renderer: a new window shows,
// tabs draw, the menu's calls exist, and no script error comes up.
#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>
#include <lua.hpp>

#include "app/lua_eq.h"
#include "core/game_dir.h"
#include "net/game_connection.h"
#include "ui/dev_screens.h"
#include "ui/lua/lua_host.h"
#include "scratch_dir.h"

using namespace hailstone;
namespace fs = std::filesystem;

namespace {

fs::path ui_dir() { return fs::path(__FILE__).parent_path().parent_path() / "ui"; }

void frame(ui::lua::LuaUi& lua) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280, 800);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    lua.frame();
    ImGui::Render();
}

} // namespace

TEST_CASE("Chat window script: new windows and tabs draw without script errors") {
    fs::path src = ui_dir() / "modern";
    REQUIRE(fs::exists(src / "chat.lua"));
    hailstone::test::ScratchDir dir_scratch("chat_ui_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "game");
    fs::copy(src, dir / "modern", fs::copy_options::recursive);

    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        GameDir game(dir / "game");
        net::GameConnection conn(game);
        ui::ChatView view;
        app::EqHost host;
        host.in_game = host.ui_ready = true;
        host.game = &game;
        host.conn = &conn;
        host.chat = &view;
        app::EqApi api(host);
        api.load_layout(dir / "defaults.ini", dir / "UI_Test_Server.ini", {});
        ui::lua::LuaUi lua(dir / "modern", dir / "modern.txt");
        lua.add_eq_api([&](lua_State* L) {
            api.register_functions(L);
            lua_pushcfunction(L, [](lua_State* S) { lua_pushboolean(S, 1); return 1; });
            lua_setfield(L, -2, "InGame");
        });
        view.add(ui::ChatChannel::Say, "hello");
        for (int i = 0; i < 3; ++i) {
            api.begin_frame();
            frame(lua);
        }
        CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
        CHECK(ImGui::FindWindowByName("###modern.Chat1") != nullptr);

        // A new window from the menu's call, a second tab, a tell tab: all draw.
        lua_State* L = lua.state();
        REQUIRE(luaL_dostring(L, "eq.ChatNewWindow('Fights'); eq.ChatNewTab(1, 'Social'); eq.ChatSetTellWindows(true)") == LUA_OK);
        view.add(ui::ChatChannel::Tell, "Bob tells you, 'hi'", 0, 257, "Bob");
        for (int i = 0; i < 3; ++i) {
            api.begin_frame();
            frame(lua);
        }
        CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
        CHECK(ImGui::FindWindowByName("###modern.Chat2") != nullptr);
        // the main window, titled by the tab that is up (the new one), is still the same ImGui window
        ImGuiWindow* main = ImGui::FindWindowByName("###modern.Chat1");
        REQUIRE(main);
        CHECK(main->Active);
        CHECK(main->ID == ImHashStr("###modern.Chat1")); // one window whatever its tab
    }
    ImGui::DestroyContext(ctx);
    fs::remove_all(dir);
}
