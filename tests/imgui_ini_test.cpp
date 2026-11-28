// ui/imgui_layer.h detail::switch_ini_settings: Dear ImGui's settings file follows the character (a per-character
// hailstone-windows.ini), nothing lost in either direction.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <imgui.h>
#include <imgui_internal.h> // FindWindowByName: where the open window is

#include "core/path_utf8.h"
#include "scratch_dir.h"
#include "ui/imgui_layer.h"

using namespace hailstone;
namespace fs = std::filesystem;

namespace {

std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

// One frame with the "Probe" window; at (x, y) when given, else where it is.
void frame(float x = -1, float y = -1) {
    ImGui::NewFrame();
    if (x >= 0) ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(120, 80), ImGuiCond_Always);
    ImGui::Begin("Probe");
    ImGui::End();
    ImGui::Render();
}

} // namespace

TEST_CASE("ImGui settings: the file follows the character; each keeps its own window places") {
    test::ScratchDir scratch("imgui_ini");
    const std::string shared = path_utf8(scratch.path() / "hailstone-windows.ini");
    const std::string mine = path_utf8(scratch.path() / "Tester-windows.ini");
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures; // (1.92: the atlas is built by NewFrame)
    io.DisplaySize = ImVec2(1000, 800);
    std::string ini; // what io.IniFilename points into, as ImGuiLayer::ini_path_

    // Before ImGui has loaded anything, only the name changes: nothing is written.
    ui::detail::switch_ini_settings(ini, shared);
    CHECK(std::string(io.IniFilename) == shared);
    CHECK_FALSE(fs::exists(scratch.path() / "hailstone-windows.ini"));

    frame(100, 200); // (the login screens: the shared file)
    // The world: the character's file. Leaving the shared one saves it; the character's, new, starts from the places
    // in memory.
    ui::detail::switch_ini_settings(ini, mine);
    CHECK(std::string(io.IniFilename) == mine);
    CHECK(read_all(scratch.path() / "hailstone-windows.ini").find("Pos=100,200") != std::string::npos);
    frame();
    ImGui::SaveIniSettingsToDisk(io.IniFilename);
    CHECK(read_all(scratch.path() / "Tester-windows.ini").find("Pos=100,200") != std::string::npos);

    // Moved in the world, then back to character select: the character's file keeps the new place, the shared one
    // its own.
    frame(300, 400);
    ui::detail::switch_ini_settings(ini, shared);
    CHECK(read_all(scratch.path() / "Tester-windows.ini").find("Pos=300,400") != std::string::npos);
    CHECK(read_all(scratch.path() / "hailstone-windows.ini").find("Pos=100,200") != std::string::npos);
    CHECK(read_all(scratch.path() / "hailstone-windows.ini").find("Pos=300,400") == std::string::npos);
    // The file loaded moves a window already open to its place there (ImGui applies loaded settings to open windows).
    REQUIRE(ImGui::FindWindowByName("Probe"));
    CHECK(ImGui::FindWindowByName("Probe")->Pos.x == 100.0f);
    CHECK(ImGui::FindWindowByName("Probe")->Pos.y == 200.0f);

    // In again: the character's file is loaded, and the same file again changes nothing.
    ui::detail::switch_ini_settings(ini, mine);
    ui::detail::switch_ini_settings(ini, mine);
    CHECK(std::string(io.IniFilename) == mine);
    CHECK(read_all(scratch.path() / "Tester-windows.ini").find("Pos=300,400") != std::string::npos);
    CHECK(ImGui::FindWindowByName("Probe")->Pos.x == 300.0f);
    // None at all: nothing is saved anywhere from then on.
    ui::detail::switch_ini_settings(ini, std::string());
    CHECK(io.IniFilename == nullptr);
    ImGui::DestroyContext(ctx);
}
