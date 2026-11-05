// ui::keyboard_for_ui (ui/imgui_layer.h): a press the windows take leaves the keys the game's, so held movement keys
// stay held; an edit box, a modal dialog and an explicit request still take them.
#include <doctest/doctest.h>

#include <functional>

#include <imgui.h>
#include <imgui_internal.h>

#include "ui/imgui_layer.h"

using namespace nmsc;

namespace {

struct Rects {
    ImVec2 button, edit; // centres
};

class Ui {
public:
    Ui() {
        ctx_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        unsigned char* px = nullptr;
        int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    }
    ~Ui() { ImGui::DestroyContext(ctx_); }
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;

    // One frame with the mouse there; returns whether the keys were the UI's in it (as ImGuiLayer::wants_keyboard).
    bool frame(ImVec2 mouse, bool down, const std::function<void()>& extra = {}) {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280, 800);
        io.DeltaTime = 1.0f / 60.0f;
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(0, down);
        const int requested = GImGui->WantCaptureKeyboardNextFrame;
        ImGui::NewFrame();
        const bool keys = ui::keyboard_for_ui(requested);
        imgui_captured = io.WantCaptureKeyboard;
        ImGui::SetNextWindowPos(ImVec2(100, 100));
        ImGui::SetNextWindowSize(ImVec2(300, 200));
        ImGui::Begin("Window", nullptr, ImGuiWindowFlags_NoSavedSettings);
        ImGui::Button("Go", ImVec2(120, 30));
        rects.button = centre();
        ImGui::InputText("##edit", text_, sizeof(text_));
        rects.edit = centre();
        if (extra) extra();
        ImGui::End();
        ImGui::Render();
        return keys;
    }
    // Pressed for two frames (the item is active from the press's frame on), then released.
    bool hold(ImVec2 at) {
        frame(at, false);
        frame(at, true);
        bool keys = frame(at, true);
        frame(at, false);
        return keys;
    }

    Rects rects;
    bool imgui_captured = false;

private:
    static ImVec2 centre() {
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    }
    ImGuiContext* ctx_ = nullptr;
    char text_[32] = {};
};

const ImVec2 kAway{900, 700};

} // namespace

TEST_CASE("UI keyboard: a button or window held by the mouse leaves the keys the game's") {
    Ui ui;
    CHECK_FALSE(ui.frame(kAway, false));
    // A button held down: ImGui would take the keyboard (its active item), the game keeps it.
    ui.frame(ui.rects.button, false);
    ui.frame(ui.rects.button, true);
    CHECK_FALSE(ui.frame(ui.rects.button, true));
    CHECK(ui.imgui_captured);
    ui.frame(ui.rects.button, false);
    CHECK_FALSE(ui.frame(kAway, false));
    // The title bar (a window drag) and the window's background.
    CHECK_FALSE(ui.hold(ImVec2(250, 105)));
    CHECK_FALSE(ui.hold(ImVec2(350, 280)));
}

TEST_CASE("UI keyboard: an edit box being typed in, a modal dialog and an explicit request take the keys") {
    Ui ui;
    ui.frame(kAway, false);
    // A click into the edit box: it keeps the keys after the button is let go.
    ui.hold(ui.rects.edit);
    CHECK(ui.frame(kAway, false));
    // A click elsewhere ends the typing.
    ui.hold(kAway);
    CHECK_FALSE(ui.frame(kAway, false));

    // A frame that asks for the keys (ui.CaptureKeyboard) has them in the next.
    ui.frame(kAway, false, [] { ImGui::SetNextFrameWantCaptureKeyboard(true); });
    CHECK(ui.frame(kAway, false));
    CHECK_FALSE(ui.frame(kAway, false));

    // A modal dialog open.
    auto modal = [] {
        if (!ImGui::IsPopupOpen("Ask")) ImGui::OpenPopup("Ask");
        if (ImGui::BeginPopupModal("Ask", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextUnformatted("?");
            ImGui::EndPopup();
        }
    };
    ui.frame(kAway, false, modal);
    CHECK(ui.frame(kAway, false, modal));
}
