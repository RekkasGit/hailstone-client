// GameConnection: the tracking window's list (CTrackingWnd; game/tracking.h): OP_Track out on show, every 6 s with
// auto update and from UseSkill(53); the reply parsed, sorted, filtered and shown; Track sends OP_TrackTarget.

#include <algorithm>

#include "core/log.h"
#include "game/combat.h"
#include "net/game_connection.h"
#include "net/packet_io.h"

namespace nmsc::net {

namespace {

std::string trim_name(const std::string& name) { return game::display_name(name); }

bool in_group_fn(const void* ctx, std::int32_t id) {
    const auto* c = static_cast<const GameConnection*>(ctx);
    const auto& w = c->world();
    auto it = w.spawns.find(static_cast<std::uint32_t>(id));
    if (it == w.spawns.end()) return id == static_cast<std::int32_t>(w.player_id);
    return it->first == w.player_id || c->group().has_member(game::display_name(it->second.name)) ||
           c->group().has_member(it->second.name);
}

} // namespace

void GameConnection::open_tracking() {
    if (state_ != ConnectionState::InZone) return;
    // CTrackingWnd::Activate: the times cleared and every entry reset (the level kept), then AboutToShow's OP_Track.
    tracking_.next_controls = tracking_.next_track = 0;
    for (auto& e : tracking_.list) {
        e.distance = -1.0f;
        e.spawn_id = 0;
        e.name.clear();
        e.sort_name.clear();
        e.position = -1;
        e.flags = 0;
    }
    // CTrackingWnd::Activate: the auto update check box from TrackAutoUpdate (T, t or 1; missing FALSE).
    tracking_.auto_update = client_options_.on(game::ClientOption::TrackAutoUpdate);
    tracking_.open = true;
    ++tracking_.revision;
    send_track();
}

void GameConnection::close_tracking() { tracking_.open = false; }

void GameConnection::set_track_auto_update(bool on) {
    tracking_.auto_update = on;
    client_options_.set(game::ClientOption::TrackAutoUpdate, on ? 1 : 0);
    save_client_option(game::ClientOption::TrackAutoUpdate);
}

void GameConnection::send_track() {
    if (state_ == ConnectionState::InZone) send_zone(make_empty(Op::Track));
}

void GameConnection::track_spawn(std::int32_t spawn_id) {
    // WndNotification, the Track button: Send_OP_TrackTarget(0x35, 0) marks tracking as the activity and
    // OP_TrackTarget {id} names the spawn; "You begin tracking %1." (12040, colour 0x10e), then the window closes.
    auto it = std::find_if(tracking_.list.begin(), tracking_.list.end(),
                           [&](const game::TrackEntry& e) { return e.spawn_id == spawn_id && spawn_id != 0; });
    if (it == tracking_.list.end()) return;
    ByteWriter w;
    w.le<std::uint32_t>(static_cast<std::uint32_t>(spawn_id));
    send_zone(make_packet(Op::TrackTarget, std::move(w)));
    tracking_.tracked = spawn_id;
    system(12040, {it->sort_name.empty() ? it->name : it->sort_name}, 0x10e);
    tracking_.open = false;
}

void GameConnection::set_track_sort(std::uint32_t order) {
    tracking_.sort_order = order;
    // DoTrackSort: a ranger's order (above 6 nothing), others order 0.
    bool ranger = profile_ && profile_->class_ == 4;
    std::uint32_t o = ranger ? order : 0;
    if (o <= 6) game::track_sort(tracking_.list, static_cast<int>(o));
    ++tracking_.revision;
}

void GameConnection::set_track_filter(const game::TrackFilter& f) {
    tracking_.filter = f;
    track_filter_all();
}

void GameConnection::track_filter_all() {
    const bool ranger = profile_ && profile_->class_ == 4;
    const int mine = pstatus_.level;
    for (auto& e : tracking_.list) {
        if (e.spawn_id == 0) continue;
        std::int32_t owner = 0;
        if (auto it = world_.spawns.find(static_cast<std::uint32_t>(e.spawn_id)); it != world_.spawns.end())
            owner = static_cast<std::int32_t>(it->second.pet_owner_id);
        auto standard = static_cast<std::uint32_t>(game::standard_con(e.level, mine));
        game::track_filter(e, standard, ranger, tracking_.filter, &in_group_fn, owner, this);
    }
    ++tracking_.revision;
}

void GameConnection::handle_track_reply(const AppPacket& p) {
    const auto* d = reinterpret_cast<const std::uint8_t*>(p.data.data());
    if (p.data.size() >= 4 &&
        (static_cast<std::uint32_t>(d[0]) | static_cast<std::uint32_t>(d[1]) << 8 | static_cast<std::uint32_t>(d[2]) << 16 |
         static_cast<std::uint32_t>(d[3]) << 24) == game::kTrackReplyTimer) {
        log::debug("track: the reply is a timer, not a list");
        return;
    }
    const bool ranger = profile_ && profile_->class_ == 4;
    game::set_tracking_list(tracking_.list, std::span<const std::uint8_t>(d, p.data.size()), ranger,
                            tracking_.sort_order, &trim_name);
    track_filter_all();
    std::size_t n = std::count_if(tracking_.list.begin(), tracking_.list.end(), [](const auto& e) { return e.position >= 0; });
    log::info("track: {} spawns in the reply", n);
    if (!tracking_.open && n > 0) {
        tracking_.open = true; // UseSkill(53) shows the window; its AboutToShow asked already
        tracking_.auto_update = client_options_.on(game::ClientOption::TrackAutoUpdate);
    }
}

std::vector<GameConnection::TrackLine> GameConnection::tracking_lines() const {
    std::vector<std::int32_t> group;
    for (const auto& m : group_.slots) {
        if (m.name.empty()) continue;
        for (const auto& [id, s] : world_.spawns)
            if (game::display_name(s.name) == m.name || s.name == m.name) group.push_back(static_cast<std::int32_t>(id));
    }
    std::vector<TrackLine> out;
    for (const auto& e : tracking_.list) {
        if (e.spawn_id == 0 || (e.flags & game::kTrackHidden)) continue;
        out.push_back({game::track_line(e), game::track_color(e, group), e.spawn_id});
    }
    return out;
}

} // namespace nmsc::net
