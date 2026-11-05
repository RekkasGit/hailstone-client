#include "ui/social_windows.h"

#include <format>

#include <imgui.h>

namespace nmsc::ui {

namespace {

constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
const ImVec4 kDim(0.65f, 0.65f, 0.65f, 1.0f);
const ImVec4 kLeader(1.0f, 0.85f, 0.30f, 1.0f);

bool input_line(const char* label, std::string& text, float width) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", text.c_str());
    ImGui::SetNextItemWidth(width);
    bool enter = ImGui::InputText(label, buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue);
    text = buf;
    return enter;
}

} // namespace

void guild_window(GuildWindowView& view, SocialActions& actions) {
    if (!view.open) return;
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(em * 34.0f, em * 24.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("Guild Management##nmsc_guild", &view.open, kPanelFlags);
    if (!view.in_guild) {
        ImGui::TextColored(kDim, "You are not in a guild.");
        ImGui::End();
        return;
    }
    ImGui::TextColored(kLeader, "%s", view.guild_name.c_str());
    if (!view.motd.empty()) ImGui::TextWrapped("MOTD (%s): %s", view.motd_set_by.c_str(), view.motd.c_str());
    ImGui::Checkbox("Show offline", &view.show_offline);
    ImGui::SameLine();
    int online = 0;
    for (const auto& m : view.members) online += m.zone.empty() ? 0 : 1;
    ImGui::TextColored(kDim, "%d members, %d online", static_cast<int>(view.members.size()), online);
    ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                         ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("members", 6, tf, ImVec2(0.0f, -em * 4.2f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Lvl");
        ImGui::TableSetupColumn("Class");
        ImGui::TableSetupColumn("Rank");
        ImGui::TableSetupColumn("Zone");
        ImGui::TableSetupColumn("Note");
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < view.members.size(); ++i) {
            const auto& m = view.members[i];
            if (!view.show_offline && m.zone.empty()) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, m.zone.empty() ? kDim : ImVec4(1, 1, 1, 1));
            if (ImGui::Selectable(m.name.c_str(), view.selected == static_cast<int>(i), ImGuiSelectableFlags_SpanAllColumns))
                view.selected = static_cast<int>(i);
            ImGui::TableNextColumn();
            ImGui::Text("%d", m.level);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.klass.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.rank.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.zone.empty() ? "offline" : m.zone.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.note.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Invite target")) actions.guild_invite = true;
    ImGui::SameLine();
    bool has_sel = view.selected >= 0 && view.selected < static_cast<int>(view.members.size());
    ImGui::BeginDisabled(!has_sel);
    if (ImGui::Button("Remove") && has_sel) actions.guild_remove = view.members[static_cast<std::size_t>(view.selected)].name;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Leave guild")) actions.guild_leave = true;
    if (input_line("##motd", view.motd_edit, em * 24.0f) && !view.motd_edit.empty()) {
        actions.guild_motd = view.motd_edit;
        view.motd_edit.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Set MOTD") && !view.motd_edit.empty()) {
        actions.guild_motd = view.motd_edit;
        view.motd_edit.clear();
    }
    ImGui::End();
}

void guild_invite_prompt(const GuildWindowView& view, SocialActions& actions) {
    if (view.inviter.empty()) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.40f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.9f);
    ImGui::Begin("Guild invitation##nmsc_guild_invite", nullptr,
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    // Handle_OP_GuildInvite's question (eqstr 534).
    ImGui::Text("%s is interested in having you join %s.", view.inviter.c_str(), view.invite_guild.c_str());
    if (ImGui::Button("Accept", ImVec2(em * 6.0f, 0.0f))) actions.guild_answer = true;
    ImGui::SameLine();
    if (ImGui::Button("Decline", ImVec2(em * 6.0f, 0.0f))) actions.guild_answer = false;
    ImGui::End();
}

void raid_window(RaidWindowView& view, SocialActions& actions) {
    if (!view.open) return;
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(em * 40.0f, em * 22.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("Raid##nmsc_raid", &view.open, kPanelFlags);
    if (!view.inviter.empty()) {
        ImGui::Text("%s invites you to join a raid.", view.inviter.c_str());
        if (ImGui::Button("Accept")) actions.raid_answer = true;
        ImGui::SameLine();
        if (ImGui::Button("Decline")) actions.raid_answer = false;
        ImGui::Separator();
    }
    if (!view.in_raid) {
        ImGui::TextColored(kDim, "You are not in a raid.");
    } else {
        ImGui::Text("Leader: %s%s", view.leader.c_str(), view.locked ? "  (locked)" : "");
        if (!view.motd.empty()) ImGui::TextWrapped("MOTD: %s", view.motd.c_str());
        // Twelve groups in a grid of four columns, then the ungrouped members.
        if (ImGui::BeginTable("groups", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(0.0f, -em * 2.2f))) {
            for (std::size_t g = 0; g < view.groups.size(); ++g) {
                ImGui::TableNextColumn();
                ImGui::TextColored(kDim, g < 12 ? "Group %d" : "Ungrouped", static_cast<int>(g + 1));
                for (const auto& m : view.groups[g]) {
                    ImVec4 c = m.leader ? kLeader : ImVec4(1, 1, 1, 1);
                    ImGui::TextColored(c, "%s%s %d %s", m.group_leader ? "*" : " ", m.name.c_str(), m.level, m.klass.c_str());
                }
            }
            ImGui::EndTable();
        }
    }
    if (ImGui::Button("Invite target")) actions.raid_invite = true;
    if (view.in_raid) {
        ImGui::SameLine();
        if (ImGui::Button("Leave raid")) actions.raid_leave = true;
    }
    ImGui::End();
}

void friends_window(FriendsWindowView& view, SocialActions& actions) {
    if (!view.open) return;
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(em * 18.0f, em * 22.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("Friends##nmsc_friends", &view.open, kPanelFlags);
    if (!view.chat_connected) ImGui::TextColored(kDim, "Not connected to the chat server.");
    if (ImGui::BeginTabBar("lists")) {
        auto list = [&](const char* tab, std::vector<std::string>& names, std::optional<std::string>& add,
                        std::optional<std::string>& remove) {
            if (!ImGui::BeginTabItem(tab)) return;
            ImGui::BeginChild("names", ImVec2(0.0f, -em * 2.2f), true);
            for (const auto& n : names) {
                ImGui::PushID(n.c_str());
                ImGui::TextUnformatted(n.c_str());
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - em * 1.2f);
                if (ImGui::SmallButton("x")) remove = n;
                ImGui::PopID();
            }
            ImGui::EndChild();
            if (input_line("##add", view.edit, em * 10.0f) && !view.edit.empty()) {
                add = view.edit;
                view.edit.clear();
            }
            ImGui::SameLine();
            if (ImGui::Button("Add") && !view.edit.empty()) {
                add = view.edit;
                view.edit.clear();
            }
            ImGui::EndTabItem();
        };
        list("Friends", view.friends, actions.add_friend, actions.remove_friend);
        list("Ignore", view.ignored, actions.add_ignore, actions.remove_ignore);
        ImGui::EndTabBar();
    }
    ImGui::End();
}

} // namespace nmsc::ui
