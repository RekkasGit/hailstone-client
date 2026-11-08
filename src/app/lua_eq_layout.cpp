// The layout windows: LoadskinWnd (CLoadskinWnd, the skins being this client's user interfaces, ui/<pack>) and
// LayoutCopyWindow (CLayoutCopyWnd, another character's or the default window layout copied over the current one).

#include <format>
#include <string>
#include <vector>

#include "app/lua_eq_windows.h"
#include "net/game_connection.h"

namespace hailstone::app {

namespace {

// CLoadskinWnd @0x6bc050 (rof2decomp export CLoadskinWnd.c): SkinList, LoadButton (disabled at first) and INIButton
// (a check box, checked at first). AboutToShow runs CLoadskinWnd_FillSkinList @0x6bc240: the folders of UIFiles\ as
// rows, the one the UI ini's [Main] UISkin names selected, Load enabled when it was found. WndNotification
// @0x6bc400: a click on the list enables Load; Load runs /loadskin "<the selected row> <1 / 0: INIButton checked>".
// Here the skins are the user interfaces (option_choices("ui_name"), ui_name_now the running one), as /loadskin
// takes them.
class LoadskinWindow : public StockWindow {
public:
    LoadskinWindow() : StockWindow("LoadskinWnd", false) {}

    void set_shown(EqHost& host, bool show) override {
        if (show && !open_) fill(host); // AboutToShow
        open_ = show;
    }
    void child(EqHost&, std::string_view id, Child& out) override {
        if (id == "LoadButton") out.enabled = load_enabled_;
        else if (id == "INIButton") out.checked = ini_;
    }
    const ListData* list(EqHost&, std::string_view id) override { return id == "SkinList" ? &list_ : nullptr; }
    bool list_click(EqHost&, std::string_view id, int row, bool, int) override {
        if (id != "SkinList" || row < 0 || row >= static_cast<int>(list_.rows.size())) return false;
        list_.selected = row;
        load_enabled_ = true;
        return true;
    }
    bool click(EqHost& host, std::string_view id, std::string_view button, bool) override {
        if (button != "left") return false;
        if (id == "INIButton") {
            ini_ = !ini_;
        } else if (id == "LoadButton") {
            if (list_.selected < 0 || !host.submit_chat) return true;
            host.submit_chat(std::format("/loadskin \"{}\" {}", list_.rows[static_cast<std::size_t>(list_.selected)].texts[0],
                                         ini_ ? 1 : 0));
        } else {
            return false;
        }
        return true;
    }

private:
    void fill(EqHost& host) {
        list_ = {};
        list_.columns = 1;
        std::vector<std::string> skins = host.option_choices ? host.option_choices("ui_name") : std::vector<std::string>{};
        int current = -1;
        if (host.find_option)
            if (auto v = host.find_option("ui_name_now")) current = *v;
        for (const auto& name : skins) list_.rows.push_back(ListRow{{name}});
        list_.selected = current >= 0 && current < static_cast<int>(skins.size()) ? current : -1;
        load_enabled_ = list_.selected >= 0;
    }

    ListData list_;
    bool load_enabled_ = false, ini_ = true;
};

// CLayoutCopyWnd @0x6b5ec0 (rof2decomp export CLayoutCopyWnd.c): LC_LayoutsList (columns File 180 and Mode 140),
// LC_CopyButton and the LC_CopyHotButton / LC_CopyHotButtonLoadouts / LC_CopySocials check boxes. AboutToShow
// (0x6b6620) fills the list from the layouts found (EqHost::layout_sources, 0x6b6580). WndNotification
// @0x6b5af0: Copy, with a row selected, asks 6332 (the window's dialog 100, Yes / No, 60 s); the yes posts message
// 0x4646 to the main window (0x6b5b80), whose handler copies the selected layout with the three boxes
// (0x6b5bb0, __CopyLayout: EqHost::copy_layout) and prints the refusal it gives.
//
// Hailstone additions, not in the original:
// - LC_CopyChatCheckBox and LC_CopyOptionsCheckBox copy the chat settings and the game options, which this client
//   keeps per character (EqHost::LayoutCopyParts); they are off for defaults.ini, which has neither.
// - activate(0) is the offer at a character's first entry into the world (ClientApp's zone-in, when it has no UI.ini
//   and the game folder no UI_<name>_<server>.ini): shown only when the option offer_layout_copy is on (hailstone.ini
//   ui.offer_layout_copy, on by default) and another character of this server has a layout. The original only opens
//   the window from the Options window and /copylayout (rof2decomp: pinstCLayoutCopyWnd's only Activate callers are
//   COptionsWnd's OGP_LayoutCopyButton and Cmd_copylayout). The offer selects the character played last and checks
//   every box, since a new character has nothing of its own to lose; LC_FirstVisitText says why the window came up
//   and LC_OfferCheckBox turns the offer off.
class LayoutCopyWindow : public StockWindow {
public:
    LayoutCopyWindow() : StockWindow("LayoutCopyWindow", false) {}

    void set_shown(EqHost& host, bool show) override {
        if (show && !open_) {
            fill(host);
            if (offered_) hot_ = loadouts_ = socials_ = chat_ = options_ = false; // the offer's boxes, not the player's
            offered_ = false;
        }
        open_ = show;
    }
    bool activate(EqHost& host, int) override {
        if (open_ || !offer_on(host)) return false;
        fill(host);
        int row = -1;
        for (std::size_t i = 0; i < sources_.size() && row < 0; ++i)
            if (!sources_[i].character_dir.empty()) row = static_cast<int>(i);
        if (row < 0) return false; // only defaults.ini: nothing better than what the character has
        list_.selected = row;
        hot_ = loadouts_ = socials_ = chat_ = options_ = true;
        offered_ = open_ = true;
        return true;
    }
    void child(EqHost& host, std::string_view id, Child& out) override {
        bool character = selected_character();
        if (id == "LC_CopyHotButtonCheckBox") out.checked = hot_;
        else if (id == "LC_CopyHotButtonLoadoutsCheckBox") out.checked = loadouts_;
        else if (id == "LC_CopySocialsCheckBox") out.checked = socials_;
        else if (id == "LC_CopyChatCheckBox") out.checked = chat_, out.enabled = character;
        else if (id == "LC_CopyOptionsCheckBox") out.checked = options_, out.enabled = character;
        else if (id == "LC_OfferCheckBox") out.checked = offer_on(host), out.shown = offered_;
        else if (id == "LC_FirstVisitText") {
            out.shown = offered_;
            out.text = "This character has no window layout yet. Start from one of your other characters on this "
                       "server, or close this window to keep the default layout.";
        }
    }
    const ListData* list(EqHost&, std::string_view id) override { return id == "LC_LayoutsList" ? &list_ : nullptr; }
    bool list_click(EqHost&, std::string_view id, int row, bool, int) override {
        if (id != "LC_LayoutsList" || row < 0 || row >= static_cast<int>(list_.rows.size())) return false;
        list_.selected = row;
        return true;
    }
    bool click(EqHost& host, std::string_view id, std::string_view button, bool) override {
        if (button != "left") return false;
        if (id == "LC_CopyHotButtonCheckBox") hot_ = !hot_;
        else if (id == "LC_CopyHotButtonLoadoutsCheckBox") loadouts_ = !loadouts_;
        else if (id == "LC_CopySocialsCheckBox") socials_ = !socials_;
        else if (id == "LC_CopyChatCheckBox") chat_ = !chat_;
        else if (id == "LC_CopyOptionsCheckBox") options_ = !options_;
        else if (id == "LC_OfferCheckBox") {
            if (host.set_option) host.set_option("offer_layout_copy", offer_on(host) ? 0 : 1);
        } else if (id == "LC_CopyButton") {
            if (list_.selected < 0 || !host.conn) return true;
            EqHost::LayoutSource from = sources_[static_cast<std::size_t>(list_.selected)];
            EqHost::LayoutCopyParts parts{hot_, loadouts_, socials_, chat_ && selected_character(),
                                          options_ && selected_character()};
            EqHost::Confirm c;
            c.text = host.conn->strings().format(6332, {});
            c.timeout_ms = 60000;
            c.done = [&host, from, parts](bool yes) {
                if (!yes || !host.copy_layout) return;
                if (auto refused = host.copy_layout(from, parts); refused && host.conn)
                    host.conn->show_eqstr(refused->first, refused->second);
            };
            host.confirm = std::move(c);
        } else {
            return false;
        }
        return true;
    }

private:
    static bool offer_on(EqHost& host) { return !host.find_option || host.find_option("offer_layout_copy").value_or(1) != 0; }
    bool selected_character() const {
        return list_.selected >= 0 && static_cast<std::size_t>(list_.selected) < sources_.size() &&
               !sources_[static_cast<std::size_t>(list_.selected)].character_dir.empty();
    }
    void fill(EqHost& host) {
        sources_ = host.layout_sources ? host.layout_sources() : std::vector<EqHost::LayoutSource>{};
        list_ = {};
        list_.columns = 2;
        list_.labels = {"File", "Mode"};
        list_.widths = {180, 140};
        for (const auto& s : sources_)
            list_.rows.push_back(ListRow{{s.world.empty() ? s.label : std::format("{} ({})", s.label, s.world), s.mode}});
    }

    std::vector<EqHost::LayoutSource> sources_;
    ListData list_;
    bool hot_ = false, loadouts_ = false, socials_ = false, chat_ = false, options_ = false;
    bool offered_ = false;
};

} // namespace

void add_layout_windows(std::vector<std::unique_ptr<StockWindow>>& out) {
    out.push_back(std::make_unique<LoadskinWindow>());
    out.push_back(std::make_unique<LayoutCopyWindow>());
}

} // namespace hailstone::app
