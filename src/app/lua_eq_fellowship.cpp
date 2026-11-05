// The fellowship window (CFellowshipWnd), the alert history (CAlertHistoryWnd) and the trophy tribute
// (CTributeTrophyWnd). None has data on an EQEmu server: EQEmu sends no fellowship (the player's SFellowship stays
// empty), no campsite kits, no alerts and no trophy tribute, so these show
// what the original shows without them, and send what the original sends.

#include <string>
#include <vector>

#include "app/lua_eq_windows.h"
#include "net/game_connection.h"

namespace nmsc::app {

namespace {

// Opcodes not in EQEmu's RoF2 table: the fellowship requests (CFellowshipWnd::WndNotification @0x669440, 0x434 bytes
// {u32 request, u32 argument, ...}) and the campsite kit request (CFellowshipWnd_RequestCampsiteKits @0x6688e0, 8 bytes
// {u32 !ShowAll, i32 the filter's bit or -1}).
constexpr std::uint16_t kOpFellowshipRequest = 0x7121, kOpCampsiteKitRequest = 0x0c12;
constexpr std::uint32_t kRequestCreate = 1;

// CFellowshipWnd @0x6689f0 (rof2decomp export CFellowshipWnd.c, re/src/game/ui/fellowship_display_eh.cpp):
// CFellowshipWnd_UpdateDisplay @0x667ed0 without a fellowship enables Create and disables Leave, Create Campsite and
// Destroy Campsite, empties the campsite viewer and the motto (disabled); Make Leader, Remove, Invite, End, Toggle
// Experience Sharing and Update MOTD are enabled only for the leader, as OnProcessFrame @0x668630 keeps Create (no
// fellowship), Make Leader and Invite (leading). The campsite filter holds "None - show all" (and the kit categories
// of a table loaded elsewhere, not built). WndNotification @0x669440: Create sends request 1; Refresh List asks for
// the campsite kits; the rest act on a fellowship.
class FellowshipWindow : public StockWindow {
public:
    FellowshipWindow() : StockWindow("FellowshipWnd", false) {
        filter_.choices = {"None - show all"}, filter_.selected = 0;
        // the XML's columns
        members_.columns = 7;
        members_.labels = {"Name", "Lvl", "Class", "L", "Zone", "Last On", "XP Sharing"};
        members_.widths = {100, 45, 60, 25, 100, 80, 80};
        kits_.columns = 2;
        kits_.labels = {"", "Campsite Item"};
        kits_.widths = {10, 500};
    }

    void child(EqHost&, std::string_view id, Child& out) override {
        if (id == "CreateFellowshipButton") out.enabled = true;
        else if (id == "LeaveFellowshipButton" || id == "CreateCampsite" || id == "DestroyCampsite" ||
                 id == "MakeLeaderButton" || id == "RemoveMemberButton" || id == "InviteMemberButton" ||
                 id == "EndFellowshipButton" || id == "ToggleExpSharingButton" || id == "UpdateMOTDButton" ||
                 id == "MOTDInput")
            out.enabled = false;
        if (id == "MOTDInput" || id == "CampsiteViewer" || id == "CampsiteDescription") out.text = "";
        else if (id == "ShowAllButton") out.checked = show_all_;
    }
    const ListData* list(EqHost&, std::string_view id) override {
        if (id == "MemberList") return &members_;
        if (id == "CampsiteKitList") return &kits_;
        return nullptr;
    }
    const ComboData* combo(EqHost&, std::string_view id) override { return id == "CampsiteFilter" ? &filter_ : nullptr; }
    bool combo_select(EqHost&, std::string_view id, int index) override {
        if (id != "CampsiteFilter" || index < 0 || index >= static_cast<int>(filter_.choices.size())) return false;
        filter_.selected = index;
        return true;
    }
    bool click(EqHost& host, std::string_view id, std::string_view button, bool) override {
        if (button != "left" || !host.conn) return false;
        if (id == "CreateFellowshipButton") {
            std::vector<std::byte> b(0x434);
            b[0] = static_cast<std::byte>(kRequestCreate);
            host.conn->send_zone_packet(net::AppPacket(kOpFellowshipRequest, std::move(b)));
        } else if (id == "RefreshListButton") {
            kits_.rows.clear();
            std::vector<std::byte> b(8);
            b[0] = static_cast<std::byte>(show_all_ ? 0 : 1);
            std::int32_t mask = -1; // the filter's "None - show all"
            for (int i = 0; i < 4; ++i) b[4 + static_cast<std::size_t>(i)] = static_cast<std::byte>((static_cast<std::uint32_t>(mask) >> (8 * i)) & 0xff);
            host.conn->send_zone_packet(net::AppPacket(kOpCampsiteKitRequest, std::move(b)));
        } else if (id == "ShowAllButton") {
            show_all_ = !show_all_;
        } else {
            return false;
        }
        return true;
    }

private:
    ListData members_, kits_;
    ComboData filter_;
    bool show_all_ = false;
};

// CAlertHistoryWnd @0x61a9e0: AH_Closed_Alert_List (the alerts closed, from the alert list the server sends with
// opcode 0x42c4, Handle_AlertListUpdate @0x412a90, and the UI ini's [AlertsWindow]) and AH_Open_Alert_Btn, enabled
// while a row is selected (OnProcessFrame @0x61ab60); Open reopens the alert (CAlertHistoryWnd_ReopenAlert). EQEmu
// sends no alerts, so the list stays empty (AlertStackWnd and AlertWnd never show).
class AlertHistoryWindow : public StockWindow {
public:
    AlertHistoryWindow() : StockWindow("AlertHistoryWnd", false) {
        list_.columns = 3; // the XML's columns
        list_.labels = {"Title", "Alert Text", ""};
        list_.widths = {100, 350, 10};
    }

    void child(EqHost&, std::string_view id, Child& out) override {
        if (id == "AH_Open_Alert_Btn") out.enabled = list_.selected != -1;
    }
    const ListData* list(EqHost&, std::string_view id) override { return id == "AH_Closed_Alert_List" ? &list_ : nullptr; }

private:
    ListData list_;
};

// CTributeTrophyWnd @0x7881d0 (rof2decomp export CTributeTrophyWnd.c): AboutToShow @0x788fe0 refuses without the
// House of Thule expansion (the profile's expansion bit 0x10000; 5422 in colour 0xd), else asks for the trophy
// tribute (opcode 0x51a9, empty; not in EQEmu's RoF2 table) and shows the personal or guild view as its radio
// buttons say. CTributeWnd_UpdateGuildControls @0x5cb130: the personal view shows the cost, time, favour and career
// favour labels, the guild view the guild's cost, time and favour with the status text and box and the Modify
// button (5424 "Modify", 5423 "Save" while editing; enabled with the guild's trophy permission). The lists
// (TTW_AvailableTrophyBenefitList, TTW_ActiveTrophyBenefitList) are the trophy benefits the server sends, which EQEmu
// does not: they stay empty, and Upgrade / Downgrade with them. The favour labels are EQTypes 117 / 118 / 122 (the
// tribute's, app/lua_eq_values.cpp); the trophy cost and time (EQTypes 297..300) are not known without the data.
class TributeTrophyWindow : public StockWindow {
public:
    TributeTrophyWindow() : StockWindow("TributeTrophyWnd", false) {
        available_.columns = 4; // the XML's columns
        available_.labels = {"Benefit Name", "Tier", "Level", "Cost"};
        available_.widths = {120, 30, 50, 50};
        active_.columns = 3;
        active_.labels = {"Benefit Name", "Tier", "Cost"};
        active_.widths = {120, 30, 50};
    }

    void set_shown(EqHost& host, bool show) override {
        if (show && !open_) {
            if (!host.conn) return;
            const auto& p = host.conn->profile();
            if (!p || (p->expansions & 0x10000) == 0) {
                host.conn->show_eqstr(5422, {}, 0xd);
                return;
            }
            host.conn->send_zone_packet(net::AppPacket(kOpTrophyTributeRequest));
        }
        open_ = show;
    }
    void child(EqHost& host, std::string_view id, Child& out) override {
        if (!host.conn) return;
        static constexpr std::string_view kPersonal[] = {"TTW_CostLabel", "TTW_TimeLabel", "TTW_CurrentPointsLabel",
                                                         "TTW_TotalPointsText", "TTW_TotalPointsLabel"};
        static constexpr std::string_view kGuild[] = {"TTW_GuildCostLabel", "TTW_GuildTimeLabel",
                                                      "TTW_GuildCurrentPointsLabel", "TTW_StatusText", "TTW_StatusButton",
                                                      "TTW_LockButton"};
        for (auto p : kPersonal)
            if (id == p) out.shown = !guild_;
        for (auto g : kGuild)
            if (id == g) out.shown = guild_;
        if (id == "TTW_ViewPersonal_Btn") out.checked = !guild_;
        else if (id == "TTW_ViewGuild_Btn") out.checked = guild_;
        else if (id == "TTW_CurrentPointsLabel") out.text = std::to_string(host.conn->tribute().points);
        else if (id == "TTW_TotalPointsLabel") out.text = std::to_string(host.conn->tribute().career);
        else if (id == "TTW_GuildCurrentPointsLabel") out.text = std::to_string(host.conn->guild_tribute().active.favor);
        else if (id == "TTW_LockButton") {
            out.text = host.conn->strings().format(5424, {});
            out.enabled = false; // the guild's trophy permission (LocalPC_HasGuildPermission(0x19)) is not kept
        } else if (id == "TTW_UpgradeButton" || id == "TTW_DowngradeButton") {
            out.enabled = false;
        } else if (id == "TTW_Description") {
            out.text = "";
        }
    }
    const ListData* list(EqHost&, std::string_view id) override {
        if (id == "TTW_AvailableTrophyBenefitList") return &available_;
        if (id == "TTW_ActiveTrophyBenefitList") return &active_;
        return nullptr;
    }
    bool click(EqHost&, std::string_view id, std::string_view button, bool) override {
        if (button != "left") return false;
        if (id == "TTW_ViewPersonal_Btn") guild_ = false;
        else if (id == "TTW_ViewGuild_Btn") guild_ = true;
        else return false;
        return true;
    }

private:
    static constexpr std::uint16_t kOpTrophyTributeRequest = 0x51a9;
    ListData available_, active_;
    bool guild_ = false;
};

} // namespace

void add_fellowship_windows(std::vector<std::unique_ptr<StockWindow>>& out) {
    out.push_back(std::make_unique<FellowshipWindow>());
    out.push_back(std::make_unique<AlertHistoryWindow>());
    out.push_back(std::make_unique<TributeTrophyWindow>());
}

} // namespace nmsc::app
