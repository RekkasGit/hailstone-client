// The map windows over the eq API (app/lua_eq_map.cpp): the toolbar's lines and labels, saved as SaveEx writes
// them, and the map window's settings in the character's UI ini.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <imgui.h>
#include <lua.hpp>

#include "app/lua_eq.h"
#include "game_files.h"
#include "scratch_dir.h"

using namespace hailstone;
namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

std::string run(lua_State* L, const std::string& code) {
    if (luaL_dostring(L, code.c_str()) != LUA_OK) {
        std::string e = lua_tostring(L, -1);
        lua_settop(L, 0);
        return "error: " + e;
    }
    std::string out = lua_isnil(L, -1) ? "nil" : luaL_tolstring(L, -1, nullptr);
    lua_settop(L, 0);
    return out;
}

// The Options window's map pack row: eq.ComboInfo / eq.ComboSelect on the map window's maps directory combo.
struct MapLua {
    app::EqHost host;
    app::EqApi api{host};
    lua_State* L = nullptr;
    MapLua(const fs::path& global_maps, const fs::path& ui_ini) {
        host.in_game = host.ui_ready = true;
        host.zone_short_name = "qeynos";
        host.global_maps_dir = global_maps;
        api.load_layout(ui_ini.parent_path() / "defaults.ini", ui_ini, {});
        L = luaL_newstate();
        luaL_openlibs(L);
        lua_newtable(L);
        api.register_functions(L);
        lua_setglobal(L, "eq");
    }
    ~MapLua() { lua_close(L); }
    MapLua(const MapLua&) = delete;
    MapLua& operator=(const MapLua&) = delete;
    std::string operator()(const std::string& code) { return run(L, code); }
    // The choices and the chosen one, as "Default,a,b|1".
    std::string packs() {
        return (*this)("local c, s = eq.ComboInfo('MapViewWnd', 'MVW_MapsDirCombobox') "
                       "return table.concat(c, ',') .. '|' .. s");
    }
};

} // namespace

TEST_CASE("Map windows: the toolbar walks a line, adds a label and saves the four layers") {
    const GameDir* game = test::game_dir();
    if (!game) return;
    ImGuiContext* ctx = ImGui::CreateContext();
    hailstone::test::ScratchDir dir_scratch("map_windows_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir);
    // The server's maps folder (servers/<profile>/maps) takes the edits.
    fs::path maps = dir / "server" / "maps";
    {
        app::EqHost host;
        host.in_game = host.ui_ready = true;
        host.game = game;
        host.zone_short_name = "qeynos";
        host.maps_dir = maps;
        host.feet_valid = true;
        host.feet[0] = 100, host.feet[1] = 50, host.feet[2] = 3; // world: map (-50, -100, 3)
        app::EqApi api(host);
        api.load_layout(dir / "defaults.ini", dir / "UI_Test_Server.ini", {});
        api.begin_frame(); // the zone's map is read

        CHECK_FALSE(api.shown("MapToolbarWnd"));
        CHECK(api.click_child("MapViewWnd", "MVW_ToolbarButton"));
        CHECK(api.shown("MapToolbarWnd"));
        CHECK(api.click_child("MapToolbarWnd", "MTW_StartLineButton"));
        host.feet[0] = 120; // map (-50, -120)
        CHECK(api.click_child("MapToolbarWnd", "MTW_AddPointButton"));
        CHECK(api.click_child("MapToolbarWnd", "MTW_EndLineButton"));
        CHECK(api.click_child("MapToolbarWnd", "MTW_AddLabelButton"));
        CHECK(api.shown("EditLabelWnd"));
        CHECK(api.click_child("EditLabelWnd", "ELW_LabelLargeButton"));
        CHECK(api.click_child("EditLabelWnd", "ELW_DoneButton"));
        CHECK_FALSE(api.shown("EditLabelWnd"));
        CHECK(api.click_child("MapToolbarWnd", "MTW_SaveMapButton"));
        CHECK(api.click_child("MapViewWnd", "MVW_ShowGroupButton")); // off, stored
        api.save_layout();
    }
    // Layer 1 (the active layer): newest first; the end point repeats (FUN_006cab70's test is against the zone
    // box's max plus the point, so a point where the line already is still adds a line).
    // (After the game's own layer 1, read with the zone.)
    std::string layer1 = slurp(maps / "qeynos_1.txt");
    CHECK(layer1.rfind("L -50.0000, -120.0000, 3.0000,  -50.0000, -120.0000, 3.0000,  0, 0, 0\n"
                       "L -50.0000, -100.0000, 3.0000,  -50.0000, -120.0000, 3.0000,  0, 0, 0\n",
                       0) == 0);
    CHECK(layer1.find("\nP ") == layer1.find("\nP -50.0000, -120.0000, 3.0000,  0, 0, 0,  3,  \n"));
    // The other layers are written too: the stock base map as read, the empty ones empty.
    std::string base = slurp(maps / "qeynos.txt");
    auto stock = game->read("maps/qeynos.txt");
    REQUIRE(stock);
    auto count = [](std::string_view t) { return std::count(t.begin(), t.end(), '\n'); };
    CHECK(count(base) == count(std::string_view(reinterpret_cast<const char*>(stock->data()), stock->size())));
    CHECK(fs::exists(maps / "qeynos_3.txt"));
    CHECK_FALSE(fs::exists(dir / "maps")); // not beside the UI ini
    // StoreIniInfo's keys.
    std::string ini = slurp(dir / "UI_Test_Server.ini");
    CHECK(ini.find("[MapViewWnd]") != std::string::npos);
    CHECK(ini.find("ShowGroup=false") != std::string::npos);
    CHECK(ini.find("LayersVisible=1111") != std::string::npos);
    CHECK(ini.find("ActiveLayer=1") != std::string::npos);

    // Read back: the player's saved layer comes before the game's.
    {
        app::EqHost host;
        host.in_game = host.ui_ready = true;
        host.game = game;
        host.zone_short_name = "qeynos";
        host.maps_dir = maps;
        app::EqApi api(host);
        api.load_layout(dir / "defaults.ini", dir / "UI_Test_Server.ini", {});
        api.begin_frame();
        api.click_child("MapViewWnd", "MVW_ToolbarButton");
        api.click_child("MapToolbarWnd", "MTW_SaveMapButton");
    }
    CHECK(slurp(maps / "qeynos_1.txt").find("P -50.0000, -120.0000, 3.0000,  0, 0, 0,  3,  \n") !=
          std::string::npos);
    fs::remove_all(dir);
    ImGui::DestroyContext(ctx);
}

TEST_CASE("Map windows: the maps directory is listed with the map closed and kept per character") {
    ImGuiContext* ctx = ImGui::CreateContext();
    hailstone::test::ScratchDir scratch("map_pack_option_test");
    const fs::path dir = scratch.path();
    const fs::path maps = dir / "maps";
    fs::create_directories(maps / "brewall");
    std::ofstream(maps / "brewall" / "qeynos.txt") << "L 0, 0, 0,  10, 10, 0,  0, 0, 0\n";
    fs::create_directories(dir / "a");
    fs::create_directories(dir / "b");
    {
        MapLua lua(maps, dir / "a" / "UI.ini");
        lua.api.begin_frame();
        CHECK_FALSE(lua.api.shown("MapViewWnd"));
        CHECK(lua.packs() == "Default,brewall|0");
        // A pack added while the list is asked for every frame shows once the asking stops and starts again (the
        // Options window opened again), not by reading the folders each frame.
        fs::create_directories(maps / "later");
        lua.api.begin_frame();
        CHECK(lua.packs() == "Default,brewall|0");
        lua.api.begin_frame();
        lua.api.begin_frame();
        CHECK(lua.packs() == "Default,brewall,later|0");
        CHECK(lua("return eq.ComboSelect('MapViewWnd', 'MVW_MapsDirCombobox', 1)") == "true");
        CHECK(lua.packs() == "Default,brewall,later|1");
        CHECK(lua("return eq.UiIni('Main', 'MapSkin')") == "brewall");
        lua.api.begin_frame(); // the map is read again from the pack
        lua.api.save_layout();
    }
    CHECK(slurp(dir / "a" / "UI.ini").find("MapSkin=brewall") != std::string::npos);
    {
        MapLua lua(maps, dir / "a" / "UI.ini"); // the same character: its pack
        lua.api.begin_frame();
        lua.api.begin_frame();
        CHECK(lua.packs() == "Default,brewall,later|1");
    }
    {
        MapLua lua(maps, dir / "b" / "UI.ini"); // another character: the default
        lua.api.begin_frame();
        lua.api.begin_frame();
        CHECK(lua.packs() == "Default,brewall,later|0");
    }
    ImGui::DestroyContext(ctx);
}

TEST_CASE("Map windows: Auto Z is set over eq.MapAutoZ and kept in the UI ini") {
    ImGuiContext* ctx = ImGui::CreateContext();
    hailstone::test::ScratchDir scratch("map_auto_z_test");
    const fs::path dir = scratch.path();
    fs::create_directories(dir / "maps");
    auto get = "local on, a, b = eq.MapAutoZ() return tostring(on) .. ',' .. a .. ',' .. b";
    {
        MapLua lua(dir / "maps", dir / "UI.ini");
        lua.api.begin_frame();
        CHECK(lua(get) == "false,30,30");
        CHECK(lua("local on, a, b = eq.MapAutoZ(true, 45) return tostring(on) .. ',' .. a .. ',' .. b") == "true,45,45");
        CHECK(lua("local _, a, b = eq.MapAutoZ(true, 99999, 0) return a .. ',' .. b") == "1000,1"); // clamped
        CHECK(lua("local _, a, b = eq.MapAutoZ(true, 45, 80) return a .. ',' .. b") == "45,80");
        lua.api.save_layout();
    }
    std::string ini = slurp(dir / "UI.ini");
    CHECK(ini.find("AutoZ=true") != std::string::npos);
    CHECK(ini.find("AutoZAbove=45") != std::string::npos);
    CHECK(ini.find("AutoZBelow=80") != std::string::npos);
    {
        MapLua lua(dir / "maps", dir / "UI.ini");
        lua.api.begin_frame();
        CHECK(lua(get) == "true,45,80");
    }
    ImGui::DestroyContext(ctx);
}

TEST_CASE("Map windows: an earlier version's AutoZRange sets both Auto Z ranges") {
    ImGuiContext* ctx = ImGui::CreateContext();
    hailstone::test::ScratchDir scratch("map_auto_z_old_test");
    const fs::path dir = scratch.path();
    fs::create_directories(dir / "maps");
    {
        std::ofstream f(dir / "UI.ini");
        f << "[MapViewWnd]\nAutoZ=true\nAutoZRange=60\n";
    }
    {
        MapLua lua(dir / "maps", dir / "UI.ini");
        lua.api.begin_frame();
        CHECK(lua("local on, a, b = eq.MapAutoZ() return tostring(on) .. ',' .. a .. ',' .. b") == "true,60,60");
    }
    ImGui::DestroyContext(ctx);
}
