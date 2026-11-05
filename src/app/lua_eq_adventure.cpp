// Expedition and adventure windows. CDynamicZoneWnd / CDZSwitchListWnd and the LDoN windows (CAdventureRequestWnd,
// CAdventureStatsWnd, CAdventureLeaderboardWnd) are not decoded; these follow their XML over EQEmu's packets
// (net/dz_packets.h, net/game_connection_dz.cpp, net/adventure_packets.h, net/game_connection_adventure.cpp).

#include <algorithm>
#include <format>
#include <iterator>
#include <string>

#include "app/lua_eq_windows.h"
#include "net/game_connection.h"

namespace nmsc::app {

namespace {

std::string dz_status(net::DzMemberStatus s) {
    switch (s) {
    case net::DzMemberStatus::Online: return "Online";
    case net::DzMemberStatus::Offline: return "Offline";
    case net::DzMemberStatus::InDynamicZone: return "In Expedition";
    case net::DzMemberStatus::LinkDead: return "Link Dead";
    default: return "Unknown";
    }
}

std::string dz_time_left(std::int64_t seconds) {
    if (seconds <= 0) return "Expired";
    std::int64_t d = seconds / 86400, h = seconds / 3600 % 24, m = seconds / 60 % 60;
    if (d) return std::format("{}d {}h {}m", d, h, m);
    if (h) return std::format("{}h {}m", h, m);
    return std::format("{}m {}s", m, seconds % 60);
}

// DynamicZoneWnd: the expedition's name, leader and member counts, its members (a click selects one for the
// leader's buttons), the character's lockouts, Quit, and the leader's section (shown to the leader): add the
// target, remove or make leader the selected member (else the target), invite the raid.
class DynamicZoneWindow : public StockWindow {
public:
    DynamicZoneWindow() : StockWindow("DynamicZoneWnd", false) {}

    bool shown(EqHost& host) override { return host.conn && host.conn->expedition().window_open; }
    void set_shown(EqHost& host, bool show) override {
        if (!host.conn) return;
        host.conn->set_expedition_window_open(show);
        if (show) host.conn->dz_list_timers(); // EQEmu answers with the lockouts
    }

    void child(EqHost& host, std::string_view id, Child& out) override {
        if (!host.conn) return;
        const auto& e = host.conn->expedition();
        bool in = e.info.assigned;
        if (id == "DZ_CurrentDZValue") out.text = in ? e.info.name : std::string();
        else if (id == "DZ_LeaderValue") out.text = in ? e.info.leader : std::string();
        else if (id == "DZ_NumPlayersValue") out.text = in ? std::to_string(e.members.size()) : std::string();
        else if (id == "DZ_MaxPlayersValue") out.text = in ? std::to_string(e.info.max_players) : std::string();
        else if (id == "DZ_Quit") out.enabled = in;
        else if (id == "DZLeaderSection") out.shown = host.conn->dz_is_leader();
        else if (id == "DZ_Leader_TargetValue") out.text = target(host);
        else if (id == "DZ_Leader_Add") out.enabled = !target(host).empty() && e.members.size() < e.info.max_players;
        else if (id == "DZ_Leader_Remove" || id == "DZ_Leader_MakeLeader") out.enabled = !subject(host).empty();
        else if (id == "DZ_Leader_InviteRaid") out.enabled = host.conn->raid().active;
        else if (id == "DZ_Leader_InviteTask") out.enabled = !host.conn->tasks().shared_members.empty();
    }

    const ListData* list(EqHost& host, std::string_view id) override {
        if (!host.conn) return nullptr;
        const auto& e = host.conn->expedition();
        if (id == "DZ_MemberList") {
            members_ = {};
            members_.columns = 2;
            members_.labels = {"Name", "Status"};
            members_.widths = {110, 80};
            for (const auto& m : e.members) {
                members_.rows.push_back(ListRow{{m.name, dz_status(m.status)}});
                if (m.status == net::DzMemberStatus::Offline || m.status == net::DzMemberStatus::LinkDead)
                    members_.rows.back().color = Rgb{0.6f, 0.6f, 0.6f};
            }
            auto it = std::find_if(e.members.begin(), e.members.end(), [&](const net::DzMember& m) { return m.name == selected_; });
            members_.selected = it == e.members.end() ? -1 : static_cast<int>(it - e.members.begin());
            return &members_;
        }
        if (id == "DZ_TimerList") {
            timers_ = {};
            timers_.columns = 3;
            timers_.labels = {"Expedition", "Event", "Time Left"};
            timers_.widths = {200, 150, 100};
            std::int64_t gone = (net::GameConnection::now_ms() - e.lockouts_at) / 1000;
            for (const auto& l : e.lockouts)
                timers_.rows.push_back(ListRow{{l.expedition, l.event, dz_time_left(static_cast<std::int64_t>(l.seconds) - gone)}});
            return &timers_;
        }
        return nullptr;
    }
    bool list_click(EqHost& host, std::string_view id, int row, bool, int) override {
        if (id != "DZ_MemberList") return id == "DZ_TimerList";
        const auto& m = host.conn ? host.conn->expedition().members : std::vector<net::DzMember>{};
        selected_ = row >= 0 && row < static_cast<int>(m.size()) ? m[static_cast<std::size_t>(row)].name : std::string();
        return true;
    }

    bool click(EqHost& host, std::string_view id, std::string_view button, bool) override {
        if (!host.conn || button != "left") return false;
        auto& c = *host.conn;
        if (id == "DZ_Quit") c.dz_quit();
        else if (id == "DZ_Leader_Add") c.dz_add_player(target(host));
        else if (id == "DZ_Leader_Remove") c.dz_remove_player(subject(host));
        else if (id == "DZ_Leader_MakeLeader") c.dz_make_leader(subject(host));
        else if (id == "DZ_Leader_InviteRaid") {
            const auto& members = c.expedition().members;
            for (const auto& r : c.raid().members) {
                bool in = std::any_of(members.begin(), members.end(), [&](const net::DzMember& m) { return m.name == r.name; });
                if (!in && r.name != c.character_name()) c.dz_add_player(r.name);
            }
        } else if (id == "DZ_Leader_InviteTask") {
            const auto& members = c.expedition().members;
            for (const auto& n : c.tasks().shared_members) {
                bool in = std::any_of(members.begin(), members.end(), [&](const net::DzMember& m) { return m.name == n; });
                if (!in && n != c.character_name()) c.dz_add_player(n);
            }
        } else return false;
        return true;
    }

private:
    // The targeted player's name ("" for none or a non-player).
    static std::string target(EqHost& host) {
        const auto& w = host.conn->world();
        auto it = w.spawns.find(w.target_id);
        if (it == w.spawns.end() || it->second.is_npc || it->second.is_corpse || w.target_id == w.player_id) return {};
        return it->second.name;
    }
    // The member the leader's Remove / Make Leader act on: the selected one, else the target.
    std::string subject(EqHost& host) {
        const auto& m = host.conn->expedition().members;
        if (std::any_of(m.begin(), m.end(), [&](const net::DzMember& x) { return x.name == selected_; })) return selected_;
        return target(host);
    }

    ListData members_, timers_;
    std::string selected_;
};

// DynamicZoneSwitchListWnd: the expeditions the character could enter here (OP_DzChooseZone, from a switch); OK enters the
// selected one, Cancel closes it.
class DzSwitchListWindow : public StockWindow {
public:
    DzSwitchListWindow() : StockWindow("DynamicZoneSwitchListWnd", false) {}

    bool shown(EqHost& host) override { return host.conn && !host.conn->expedition().choices.empty(); }
    void set_shown(EqHost& host, bool show) override {
        if (!show && host.conn) host.conn->dz_choose_zone(-1);
    }
    void child(EqHost& host, std::string_view id, Child& out) override {
        if (host.conn && id == "DZSwitchList_OK")
            out.enabled = selected_ >= 0 && selected_ < static_cast<int>(host.conn->expedition().choices.size());
    }
    const ListData* list(EqHost& host, std::string_view id) override {
        if (!host.conn || id != "DZSwitchList_List") return nullptr;
        list_ = {};
        list_.columns = 2;
        list_.labels = {"Expedition", "Leader"};
        list_.widths = {200, 100};
        for (const auto& c : host.conn->expedition().choices) list_.rows.push_back(ListRow{{c.description, c.leader}});
        list_.selected = selected_ < static_cast<int>(list_.rows.size()) ? selected_ : -1;
        return &list_;
    }
    bool list_click(EqHost&, std::string_view id, int row, bool, int) override {
        if (id != "DZSwitchList_List") return false;
        selected_ = row;
        return true;
    }
    bool click(EqHost& host, std::string_view id, std::string_view button, bool) override {
        if (!host.conn || button != "left") return false;
        if (id == "DZSwitchList_OK") host.conn->dz_choose_zone(selected_);
        else if (id == "DZSwitchList_Cancel") host.conn->dz_choose_zone(-1);
        else return false;
        selected_ = -1;
        return true;
    }

private:
    ListData list_;
    int selected_ = -1;
};


constexpr const char* kAdventureThemes[net::kAdventureThemes] = {"All Themes", "Deepest Guk", "Miragul's Menagerie",
                                                                 "Mistmoore Catacombs", "Rujarkian Hills", "Takish-Hiz"};

std::string percent(std::uint32_t won, std::uint32_t lost) {
    return won + lost ? std::format("{:.1f}%", 100.0 * won / (won + lost)) : std::string("-");
}

// AdventureRequestWnd: the recruiter's words (or the adventure offered, or the current adventure's description),
// the objective and risk to ask for, Request, Accept / Decline for an offer (Decline leaves the current adventure
// when there is no offer), the current adventure's times and progress, and View Stats.
class AdventureRequestWindow : public StockWindow {
public:
    AdventureRequestWindow() : StockWindow("AdventureRequestWnd", false) {}

    bool shown(EqHost& host) override { return host.conn && host.conn->adventure().recruiter; }
    void set_shown(EqHost& host, bool show) override {
        if (!show && host.conn) host.conn->close_adventure_recruiter();
    }

    void child(EqHost& host, std::string_view id, Child& out) override {
        if (!host.conn) return;
        const auto& a = host.conn->adventure();
        bool in = a.data.assigned;
        std::int64_t gone = (net::GameConnection::now_ms() - a.data_at) / 1000;
        if (id == "AdvRqst_NPCText") {
            const std::string& t = !a.offer.empty() ? a.offer : in && !a.data.text.empty() ? a.data.text : a.npc_text;
            out.text = stml_escape(t);
        } else if (id == "AdvRqst_RequestButton") {
            out.enabled = !in && a.offer.empty();
        } else if (id == "AdvRqst_AcceptButton") {
            out.enabled = !a.offer.empty();
        } else if (id == "AdvRqst_DeclineButton") {
            out.enabled = !a.offer.empty() || in; // on an adventure it leaves it (this client's use of the button)
        } else if (id == "AdvRqst_EnterTimeLeftLabel") {
            out.text = in && a.data.time_to_enter ? dz_time_left(static_cast<std::int64_t>(a.data.time_to_enter) - gone) : std::string();
        } else if (id == "AdvRqst_CompleteTimeLeftLabel") {
            out.text = in && !a.data.time_to_enter ? dz_time_left(static_cast<std::int64_t>(a.data.time_left) - gone) : std::string();
        } else if (id == "AdvRqst_ProgressTextLabel") {
            out.text = in && a.count.total ? std::format("{} / {}", a.count.current, a.count.total) : std::string();
        } else if (id == "AdvRqst_TypeCombobox" || id == "AdvRqst_RiskCombobox") {
            out.enabled = !in && a.offer.empty();
        }
    }

    const ComboData* combo(EqHost&, std::string_view id) override {
        if (id == "AdvRqst_TypeCombobox") {
            type_.choices = {"Any Type", "Assassinate", "Kill", "Collect", "Rescue"}; // EQEmu's AdventureObjective
            if (type_.selected < 0) type_.selected = 0;
            return &type_;
        }
        if (id == "AdvRqst_RiskCombobox") {
            risk_.choices = {"Normal", "High"};
            if (risk_.selected < 0) risk_.selected = 0;
            return &risk_;
        }
        return nullptr;
    }
    bool combo_select(EqHost&, std::string_view id, int index) override {
        if (id == "AdvRqst_TypeCombobox") type_.selected = index;
        else if (id == "AdvRqst_RiskCombobox") risk_.selected = index;
        else return false;
        return true;
    }

    bool click(EqHost& host, std::string_view id, std::string_view button, bool) override {
        if (!host.conn || button != "left") return false;
        auto& c = *host.conn;
        if (id == "AdvRqst_RequestButton")
            c.adventure_request(static_cast<std::uint32_t>(std::max(risk_.selected, 0) + 1),
                                static_cast<std::uint32_t>(std::max(type_.selected, 0)));
        else if (id == "AdvRqst_AcceptButton") c.adventure_answer(true);
        else if (id == "AdvRqst_DeclineButton") {
            if (!c.adventure().offer.empty()) c.adventure_answer(false);
            else c.adventure_leave();
        }
        else if (id == "AdvRqst_ViewStatsButton") c.set_adventure_stats_open(true);
        else return false;
        return true;
    }

private:
    ComboData type_, risk_;
};

// AdventureStatsWnd: successes, failures and both ranks for each theme (OP_AdventureStatsReply), the points earned
// in each and the points available (OP_AdventurePointsUpdate); Refresh asks again, Leaderboard opens that window.
class AdventureStatsWindow : public StockWindow {
public:
    AdventureStatsWindow() : StockWindow("AdventureStatsWnd", false) {}

    bool shown(EqHost& host) override { return host.conn && host.conn->adventure().stats_open; }
    void set_shown(EqHost& host, bool show) override {
        if (host.conn) host.conn->set_adventure_stats_open(show);
    }
    void child(EqHost& host, std::string_view id, Child& out) override {
        if (host.conn && id == "AdvStats_AvailableValue") out.text = std::to_string(host.conn->adventure().points.available);
    }
    const ListData* list(EqHost& host, std::string_view id) override {
        if (!host.conn || id != "AdvStats_ThemeList") return nullptr;
        const auto& a = host.conn->adventure();
        list_ = {};
        list_.columns = 6;
        list_.labels = {"Theme", "Successes", "Failures", "Rank", "Rank (%)", "Points"};
        list_.widths = {150, 70, 70, 60, 60, 60};
        std::uint32_t total_points = 0;
        for (auto p : a.points.theme) total_points += p;
        for (int t = 0; t < net::kAdventureThemes; ++t) {
            auto n = [&](const std::array<std::uint32_t, net::kAdventureThemes>& col) {
                return a.stats ? std::to_string(col[static_cast<std::size_t>(t)]) : std::string("-");
            };
            std::uint32_t points = t ? a.points.theme[static_cast<std::size_t>(t - 1)] : total_points;
            list_.rows.push_back(ListRow{{kAdventureThemes[t], a.stats ? n(a.stats->success) : "-", a.stats ? n(a.stats->failure) : "-",
                                          a.stats ? n(a.stats->rank) : "-", a.stats ? n(a.stats->rank_percent) : "-",
                                          std::to_string(points)}});
        }
        return &list_;
    }
    bool list_click(EqHost&, std::string_view id, int, bool, int) override { return id == "AdvStats_ThemeList"; }
    bool click(EqHost& host, std::string_view id, std::string_view button, bool) override {
        if (!host.conn || button != "left") return false;
        if (id == "AdvStats_RefreshButton") host.conn->adventure_stats_request();
        else if (id == "AdvStats_LeaderboardButton") host.conn->set_adventure_leaderboard_open(true);
        else if (id == "AdvStats_DoneButton") host.conn->set_adventure_stats_open(false);
        else return false;
        return true;
    }

private:
    ListData list_;
};

// AdventureLeaderboardWnd: the top 100 by the theme picked in Select (OP_AdventureLeaderboardRequest) and our own
// line. EQEmu orders by wins or by percentage and reads no risk, so the second combo box picks the order (this
// client's use of it).
class AdventureLeaderboardWindow : public StockWindow {
public:
    AdventureLeaderboardWindow() : StockWindow("AdventureLeaderboardWnd", false) {}

    bool shown(EqHost& host) override { return host.conn && host.conn->adventure().leaderboard_open; }
    void set_shown(EqHost& host, bool show) override {
        if (host.conn) host.conn->set_adventure_leaderboard_open(show);
    }
    void child(EqHost& host, std::string_view id, Child& out) override {
        if (!host.conn) return;
        const auto& b = host.conn->adventure().leaderboard;
        if (id == "AdvLeaderboard_YourNameLabel") out.text = host.conn->character_name();
        else if (id == "AdvLeaderboard_YourRankLabel") out.text = b ? std::to_string(b->rank) : std::string();
        else if (id == "AdvLeaderboard_YourSuccessLabel") out.text = b ? std::to_string(b->success) : std::string();
        else if (id == "AdvLeaderboard_YourFailureLabel") out.text = b ? std::to_string(b->failure) : std::string();
        else if (id == "AdvLeaderboard_YourPercentLabel") out.text = b ? percent(b->success, b->failure) : std::string();
    }
    const ComboData* combo(EqHost&, std::string_view id) override {
        if (id == "AdvLeaderboard_SelectCombobox") {
            theme_.choices.assign(std::begin(kAdventureThemes), std::end(kAdventureThemes));
            if (theme_.selected < 0) theme_.selected = 0;
            return &theme_;
        }
        if (id == "AdvLeaderboard_RiskCombobox") {
            order_.choices = {"By Successes", "By Percentage"};
            if (order_.selected < 0) order_.selected = 0;
            return &order_;
        }
        return nullptr;
    }
    bool combo_select(EqHost& host, std::string_view id, int index) override {
        if (id == "AdvLeaderboard_SelectCombobox") theme_.selected = index;
        else if (id == "AdvLeaderboard_RiskCombobox") order_.selected = index;
        else return false;
        request(host);
        return true;
    }
    const ListData* list(EqHost& host, std::string_view id) override {
        if (!host.conn || id != "AdvLeaderboard_LeaderList") return nullptr;
        list_ = {};
        list_.columns = 5;
        list_.labels = {"Rank", "Name", "Successes", "Failures", "Percent"};
        list_.widths = {45, 120, 60, 60, 60};
        if (const auto& b = host.conn->adventure().leaderboard) {
            int rank = 0;
            for (const auto& r : b->rows)
                list_.rows.push_back(ListRow{{std::to_string(++rank), r.name, std::to_string(r.success), std::to_string(r.failure),
                                              percent(r.success, r.failure)}});
        }
        return &list_;
    }
    bool list_click(EqHost&, std::string_view id, int, bool, int) override { return id == "AdvLeaderboard_LeaderList"; }
    bool click(EqHost& host, std::string_view id, std::string_view button, bool) override {
        if (!host.conn || button != "left") return false;
        if (id == "AdvLeaderboard_RefreshButton") request(host);
        else if (id == "AdvLeaderboard_DoneButton") host.conn->set_adventure_leaderboard_open(false);
        else return false;
        return true;
    }

private:
    void request(EqHost& host) {
        if (host.conn)
            host.conn->adventure_leaderboard_request(static_cast<std::uint32_t>(std::max(order_.selected, 0)),
                                                     static_cast<std::uint32_t>(std::max(theme_.selected, 0)), 1);
    }
    ComboData theme_, order_;
    ListData list_;
};


// PvPStatsWnd and PvpLeaderboardWnd (not decoded; their XML over EQEmu's OP_PVPStats and leaderboard, which EQEmu
// sends empty). The stats' labels carry their XML words with the numbers after them.
class PvPStatsWindow : public StockWindow {
public:
    PvPStatsWindow() : StockWindow("PvPStatsWnd", false) {}

    bool shown(EqHost& host) override { return host.conn && host.conn->pvp().stats_open; }
    void set_shown(EqHost& host, bool show) override {
        if (host.conn) host.conn->set_pvp_stats_open(show);
    }
    void child(EqHost& host, std::string_view id, Child& out) override {
        if (!host.conn) return;
        // Before the server has sent the stats (EQEmu sends them at zone-in on PvP servers only) every count reads 0.
        static const net::PvPStats kNone{};
        const auto& stats = host.conn->pvp().stats;
        const net::PvPStats* s = stats ? &*stats : &kNone;
        auto n = [&](const char* word, std::uint32_t v) { out.text = std::format("{} {}", word, v); };
        if (id == "PvpStats_AvailableValue") out.text = std::to_string(s->points);
        else if (id == "PvpStats_TotalPointsLabel") n("Total PvP Points:", s->career_points);
        else if (id == "PvpStats_YourKillsLabel") n("Kills:", s->kills);
        else if (id == "PvpStats_YourDeathsLabel") n("Deaths:", s->deaths);
        else if (id == "PvpStats_YourVitalityLabel") n("Vitality", s->vitality);
        else if (id == "PvpStats_YourInfamyLevelLabel") n("Infamy:", s->infamy);
        else if (id == "PvpStats_YourCurrentStreakLabel") n("Current Streak:", s->current_streak);
        else if (id == "PvpStats_YourKillStreakLabel") n("Best Kill Streak:", s->best_streak);
        else if (id == "PvpStats_YourDeathStreakLabel") n("Worst Death Streak:", s->worst_streak);
    }
    const ListData* list(EqHost& host, std::string_view id) override {
        if (!host.conn) return nullptr;
        const auto& s = host.conn->pvp().stats;
        auto fill = [&](ListData& l, const std::vector<const net::PvPEntry*>& rows) -> const ListData* {
            l = {};
            l.columns = 3;
            l.labels = {"Name", "Level", "Points"};
            l.widths = {160, 50, 60};
            for (const auto* e : rows)
                if (e && !e->name.empty()) l.rows.push_back(ListRow{{e->name, std::to_string(e->level), std::to_string(e->points)}});
            return &l;
        };
        if (id == "PvpStats_LastKillList") return fill(kill_, {s ? &s->last_kill : nullptr});
        if (id == "PvpStats_LastDeathList") return fill(death_, {s ? &s->last_death : nullptr});
        if (id == "PvpStats_KillsList") {
            std::vector<const net::PvPEntry*> rows;
            if (s)
                for (const auto& e : s->recent_kills) rows.push_back(&e);
            return fill(recent_, rows);
        }
        return nullptr;
    }
    bool list_click(EqHost&, std::string_view id, int, bool, int) override { return id.find("List") != std::string_view::npos; }
    bool click(EqHost& host, std::string_view id, std::string_view button, bool) override {
        if (!host.conn || button != "left") return false;
        // EQEmu sends the stats at zone-in and when the points change; it takes no request for them.
        if (id == "PvpStats_RefreshButton") return true;
        if (id == "PvpStats_LeaderboardButton") host.conn->set_pvp_leaderboard_open(true);
        else if (id == "PvpStats_DoneButton") host.conn->set_pvp_stats_open(false);
        else return false;
        return true;
    }

private:
    ListData kill_, death_, recent_;
};

class PvPLeaderboardWindow : public StockWindow {
public:
    PvPLeaderboardWindow() : StockWindow("PvpLeaderboardWnd", false) {}

    bool shown(EqHost& host) override { return host.conn && host.conn->pvp().leaderboard_open; }
    void set_shown(EqHost& host, bool show) override {
        if (host.conn) host.conn->set_pvp_leaderboard_open(show);
    }
    void child(EqHost& host, std::string_view id, Child& out) override {
        if (!host.conn) return;
        const auto& b = host.conn->pvp().leaderboard;
        if (id == "PvpLeaderboard_YourNameLabel") out.text = host.conn->character_name();
        if (!b) return;
        if (id == "PvpLeaderboard_YourRankLabel") out.text = std::to_string(b->rank);
        else if (id == "PvpLeaderboard_YourKillsLabel") out.text = std::to_string(b->kills);
        else if (id == "PvpLeaderboard_YourDeathsLabel") out.text = std::to_string(b->deaths);
        else if (id == "PvpLeaderboard_YourPointsLabel") out.text = std::to_string(b->points);
        else if (id == "PvpLeaderboard_YourInfamyLabel") out.text = std::to_string(b->infamy);
    }
    const ComboData* combo(EqHost&, std::string_view id) override {
        if (id != "PvpLeaderboard_SelectCombobox") return nullptr;
        sort_.choices = {"Kills", "Points", "Infamy"}; // EQEmu's PVPSortByKills / Points / Infamy
        if (sort_.selected < 0) sort_.selected = 0;
        return &sort_;
    }
    bool combo_select(EqHost& host, std::string_view id, int index) override {
        if (id != "PvpLeaderboard_SelectCombobox") return false;
        sort_.selected = index;
        if (host.conn) host.conn->pvp_leaderboard_request(static_cast<std::uint32_t>(index));
        return true;
    }
    const ListData* list(EqHost& host, std::string_view id) override {
        if (!host.conn || id != "PvpLeaderboard_LeaderList") return nullptr;
        list_ = {};
        list_.columns = 6;
        list_.labels = {"Rank", "Name", "Kills", "Deaths", "Points", "Infamy"};
        list_.widths = {40, 120, 50, 50, 50, 50};
        if (const auto& b = host.conn->pvp().leaderboard) {
            int rank = 0;
            for (const auto& r : b->rows)
                list_.rows.push_back(ListRow{{std::to_string(++rank), r.name, std::to_string(r.kills), std::to_string(r.deaths),
                                              std::to_string(r.points), std::to_string(r.infamy)}});
        }
        return &list_;
    }
    bool list_click(EqHost&, std::string_view id, int, bool, int) override { return id == "PvpLeaderboard_LeaderList"; }
    bool click(EqHost& host, std::string_view id, std::string_view button, bool) override {
        if (!host.conn || button != "left") return false;
        if (id == "PvpLeaderboard_RefreshButton") host.conn->pvp_leaderboard_request(static_cast<std::uint32_t>(std::max(sort_.selected, 0)));
        else if (id == "PvpLeaderboard_DoneButton") host.conn->set_pvp_leaderboard_open(false);
        else return false;
        return true;
    }

private:
    ComboData sort_;
    ListData list_;
};

} // namespace

void add_adventure_windows(std::vector<std::unique_ptr<StockWindow>>& out) {
    out.push_back(std::make_unique<DynamicZoneWindow>());
    out.push_back(std::make_unique<DzSwitchListWindow>());
    out.push_back(std::make_unique<AdventureRequestWindow>());
    out.push_back(std::make_unique<AdventureStatsWindow>());
    out.push_back(std::make_unique<AdventureLeaderboardWindow>());
    out.push_back(std::make_unique<PvPStatsWindow>());
    out.push_back(std::make_unique<PvPLeaderboardWindow>());
}

} // namespace nmsc::app
