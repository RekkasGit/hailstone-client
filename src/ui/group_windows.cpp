#include "ui/group_windows.h"

#include <algorithm>
#include <format>

#include <imgui.h>

namespace nmsc::ui {

namespace {

constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
const ImVec4 kDim(0.65f, 0.65f, 0.65f, 1.0f);
const ImVec4 kLeader(1.0f, 0.85f, 0.30f, 1.0f);
const ImVec4 kHpColor(0.80f, 0.18f, 0.18f, 1.0f);
const ImVec4 kManaColor(0.20f, 0.35f, 0.90f, 1.0f);
const ImVec4 kEndColor(0.85f, 0.75f, 0.15f, 1.0f);

void bar(float fraction, ImVec4 color, const std::string& text, float height) {
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
    ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(-1.0f, height), text.c_str());
    ImGui::PopStyleColor();
}

} // namespace

void group_window(GroupWindowView& view, GroupActions& actions) {
    if (!view.open || !view.in_group) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float em = ImGui::GetFontSize();
    // Right-hand column, under the spawn list.
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - em * 16.6f, vp->WorkPos.y + em * 23.5f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(16.0f * em, 0.0f), ImVec2(16.0f * em, 40.0f * em));
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::Begin("Group##nmsc_group", &view.open, kPanelFlags | ImGuiWindowFlags_AlwaysAutoResize);
    for (std::size_t i = 0; i < view.members.size(); ++i) {
        const auto& m = view.members[i];
        ImGui::PushID(static_cast<int>(i));
        std::string label = m.name;
        if (m.level > 0) label += std::format("  L{}", m.level);
        ImGui::PushStyleColor(ImGuiCol_Text, m.leader ? kLeader : (m.spawn_id ? ImVec4(1, 1, 1, 1) : kDim));
        if (ImGui::Selectable(label.c_str(), m.targeted) && m.spawn_id) actions.target_spawn = m.spawn_id;
        ImGui::PopStyleColor();
        if (m.leader) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - em * 3.2f);
            ImGui::TextColored(kLeader, "Leader");
        }
        float h = em * 0.75f;
        if (m.hp_percent >= 0) bar(static_cast<float>(m.hp_percent) / 100.0f, kHpColor, std::format("{}%", m.hp_percent), h);
        else bar(0.0f, kHpColor, m.offline ? "offline" : "not in zone", h);
        if (m.mana_percent >= 0) bar(static_cast<float>(m.mana_percent) / 100.0f, kManaColor, "", h * 0.45f);
        if (m.endurance_percent >= 0)
            bar(static_cast<float>(m.endurance_percent) / 100.0f, kEndColor, "", h * 0.4f);
        ImGui::PopID();
    }
    ImGui::Separator();
    if (ImGui::Button(view.inviter.empty() ? "Invite" : "Follow")) actions.invite = true;
    ImGui::SameLine();
    if (ImGui::Button("Disband")) actions.disband = true;
    if (!view.inviter.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Decline")) actions.decline = true;
    }
    ImGui::End();
}

void invite_prompt(const GroupWindowView& view, GroupActions& actions) {
    if (view.inviter.empty()) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.35f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.9f);
    ImGui::Begin("Group invitation##nmsc_invite", nullptr,
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::Text("%s invites you to join a group.", view.inviter.c_str());
    if (ImGui::Button("Follow", ImVec2(em * 6.0f, 0.0f))) actions.accept = true;
    ImGui::SameLine();
    if (ImGui::Button("Decline", ImVec2(em * 6.0f, 0.0f))) actions.decline = true;
    ImGui::End();
}

void pet_window(PetWindowView& view, GroupActions& actions) {
    if (!view.open || (!view.has_pet && view.pets.empty())) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + em * 17.2f, vp->WorkPos.y + em * 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(17.0f * em, 0.0f), ImVec2(17.0f * em, 40.0f * em));
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::Begin("Pet##nmsc_pet", &view.open, kPanelFlags | ImGuiWindowFlags_AlwaysAutoResize);
    if (view.has_pet) {
        ImGui::Text("%s", view.name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(kDim, "L%d", view.level);
        bar(static_cast<float>(view.hp_percent) / 100.0f, kHpColor, std::format("{}%", view.hp_percent), em * 0.9f);
        if (!view.target.empty()) {
            ImGui::TextColored(kDim, "Target:");
            ImGui::SameLine();
            ImGui::TextUnformatted(view.target.c_str());
            if (view.target_hp >= 0)
                bar(static_cast<float>(view.target_hp) / 100.0f, kHpColor, std::format("{}%", view.target_hp), em * 0.6f);
        } else {
            ImGui::TextColored(kDim, "No target");
        }
        // The command buttons, three per row.
        int col = 0;
        for (const auto& b : view.buttons) {
            if (col++ % 3) ImGui::SameLine();
            if (b.toggled) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(b.label, ImVec2(em * 5.2f, 0.0f))) actions.pet_command = b.command;
            if (b.toggled) ImGui::PopStyleColor();
        }
        if (!view.buffs.empty()) {
            ImGui::Separator();
            for (const auto& b : view.buffs) {
                ImGui::TextUnformatted(b.name.c_str());
                if (!b.time_left.empty()) {
                    ImGui::SameLine();
                    ImGui::TextColored(kDim, "%s", b.time_left.c_str());
                }
            }
        }
    } else {
        ImGui::TextColored(kDim, "No pet.");
    }
    if (view.pets.size() > 1) {
        ImGui::Separator();
        ImGui::TextColored(kDim, "Pets (click to command):");
        for (const auto& p : view.pets) {
            ImGui::PushID(static_cast<int>(p.spawn_id));
            std::string label = p.name.empty() ? std::format("#{}", p.spawn_id) : p.name;
            if (!p.klass.empty()) label += " (" + p.klass + ")";
            if (ImGui::Selectable(label.c_str(), p.focused) && !p.focused) actions.focus_pet = p.spawn_id;
            if (p.hp_percent >= 0)
                bar(static_cast<float>(p.hp_percent) / 100.0f, kHpColor, std::format("{}%", p.hp_percent), em * 0.5f);
            ImGui::PopID();
        }
    }
    ImGui::End();
}

} // namespace nmsc::ui
