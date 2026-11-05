// The zone guide's windows over the zone guide data the server sends: FindLocationWnd (CFindLocationWnd) and
// ZonePathWnd (CZonePathWnd). EQEmu sends none of it (no zone guide, find location or zone connection packets), so
// these windows show what the original shows without it.

#include <chrono>
#include <string>

#include "app/lua_eq_windows.h"
#include "net/game_connection.h"

namespace nmsc::app {

namespace {

// Opcodes not in EQEmu's RoF2 table: CFindLocationWnd_RequestLocations @0x66ba80 sends both, empty, once per zone.
constexpr std::uint16_t kOpFindLocationRequest = 0x2a92, kOpFindZoneConnectionRequest = 0x6d6e;

// CFindLocationWnd @0x66c560 (rof2decomp export CFindLocationWnd.c): FLW_FindLocationList (sortable, "LocationList"
// columns kept in the UI ini), FLW_FilterLocationsCheckbox (checked at first: only destinations the player can use)
// and FLW_FoundNoneLabel. AboutToShow @0x66be60 hides the list while nothing is known and the "nothing found" label,
// and asks for the zone's locations (RequestLocations: once per zone, again after OnZone); OnProcessFrame @0x66b9d0,
// once a second while shown, shows the label when the request has had no answer (no players, points of interest or
// zone connections). A row clicked starts the path to it (CFindLocationWnd_FindSelected) and hides the window.
// The answers (CFindLocationWnd_HandleLocationList and the player / connection lists) are not decoded: EQEmu has
// none (docs/unknowns.md), so the list stays empty.
class FindLocationWindow : public StockWindow {
public:
    FindLocationWindow() : StockWindow("FindLocationWnd", false) {
        list_.columns = 2; // the XML's columns
        list_.labels = {"Category", "Description"};
        list_.widths = {100, 125};
    }

    void set_shown(EqHost& host, bool show) override {
        if (show && !open_) {
            none_shown_ = false;
            waiting_ = true;
            request(host);
        }
        open_ = show;
    }
    void frame(EqHost& host) override {
        // A find's reply closes the window (OP_FindPersonReply: Show(false) before PlayerPath_HandleFindPersonReply).
        if (host.conn && host.conn->find_replies() != replies_) {
            replies_ = host.conn->find_replies();
            open_ = false;
        }
        if (host.zone_short_name != zone_) { // OnZone: the lists cleared, a new request
            zone_ = host.zone_short_name;
            requested_ = false;
            if (open_) request(host);
        }
        if (!open_) return;
        auto now = std::chrono::steady_clock::now();
        if (now - last_update_ < std::chrono::seconds(1)) return;
        last_update_ = now;
        if (waiting_) { // no players, points of interest or zone connections came
            none_shown_ = true;
            waiting_ = false;
        }
    }
    void child(EqHost&, std::string_view id, Child& out) override {
        if (id == "FLW_FoundNoneLabel") out.shown = none_shown_;
        else if (id == "FLW_FindLocationList") out.shown = false; // nothing known
        else if (id == "FLW_FilterLocationsCheckbox") out.checked = filter_;
    }
    const ListData* list(EqHost&, std::string_view id) override { return id == "FLW_FindLocationList" ? &list_ : nullptr; }
    bool click(EqHost&, std::string_view id, std::string_view button, bool) override {
        if (button != "left" || id != "FLW_FilterLocationsCheckbox") return false;
        filter_ = !filter_; // CFindLocationWnd_RefreshList (nothing to filter)
        return true;
    }

private:
    void request(EqHost& host) {
        if (requested_ || !host.conn) return;
        requested_ = true;
        host.conn->send_zone_packet(net::AppPacket(kOpFindLocationRequest));
        host.conn->send_zone_packet(net::AppPacket(kOpFindZoneConnectionRequest));
    }

    ListData list_;
    std::string zone_;
    bool requested_ = false, waiting_ = false, none_shown_ = false, filter_ = true;
    std::uint32_t replies_ = 0;
    std::chrono::steady_clock::time_point last_update_{};
};

// CZonePathWnd @0x6073c0: ZPW_ZoneList, the steps of the zone guide's active path (ZoneGuideManagerClient +0x8438).
// AboutToShow @0x6079a0 refuses to show without one (5468 in colour 0xd), and OnProcessFrame hides the window when
// the path goes. EQEmu sends no zone guide, so there is never an active path.
class ZonePathWindow : public StockWindow {
public:
    ZonePathWindow() : StockWindow("ZonePathWnd", false) {}

    void set_shown(EqHost& host, bool show) override {
        if (show && !open_) {
            if (host.conn) host.conn->show_eqstr(5468, {}, 0xd);
            return;
        }
        open_ = show;
    }
};

} // namespace

void add_zone_guide_windows(std::vector<std::unique_ptr<StockWindow>>& out) {
    out.push_back(std::make_unique<FindLocationWindow>());
    out.push_back(std::make_unique<ZonePathWindow>());
}

} // namespace nmsc::app
