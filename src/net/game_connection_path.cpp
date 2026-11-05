// GameConnection: the find path (game/player_path.h). The finds send OP_FindPersonRequest, the server answers with
// its path (EQEmu's pathing, or a straight line without it), and PlayerPath_Update runs every frame on the player's
// position.

#include <cctype>

#include "core/game_dir.h"
#include "core/ini_file.h"
#include "core/log.h"
#include "game/follow.h"
#include "net/game_connection.h"
#include "net/movement_packets.h"

namespace nmsc::net {

const std::vector<glm::vec3>& GameConnection::find_path() const {
    static const std::vector<glm::vec3> none;
    return path_.active() ? path_.points() : none;
}

void GameConnection::play_wave(std::uint32_t id) {
    SoundCue c;
    c.kind = SoundCue::Kind::Wave;
    c.spell = id;
    sound_cues_.push_back(c);
}

void GameConnection::send_find(const game::FindRequest& r, bool found) {
    // A find stopped the path first (PlayerPath_Stop: its abort sound), and reads SpellEffects\Path.ini again after
    // the request (PlayerPath_LoadSettings; FloorOffset times the player's AvatarHeight). The file is read once here.
    const game::Spawn* me = world_.player();
    if (found) {
        play_wave(game::kPathSoundAbort);
        if (!path_settings_loaded_) {
            path_settings_loaded_ = true;
            IniFile ini;
            if (auto p = game_.find("SpellEffects/Path.ini")) ini.load(*p);
            path_settings_ = game::load_path_settings(&ini);
        }
        path_.set_settings(path_settings_, me ? me->heights.avatar_height : 6.0f);
    }
    if (state_ != ConnectionState::InZone || !me) return;
    FindPersonRequest fr;
    fr.kind = static_cast<std::uint32_t>(r.kind);
    fr.id = r.id;
    fr.item_a = r.item_a;
    fr.item_b = r.item_b;
    fr.at = motion_known_ ? position_ : me->position;
    fr.point = r.point;
    send_zone(encode(fr));
}

void GameConnection::find_person_request() {
    // FUN_0059a9c0 (zoning's position reset): the request again while a path is shown.
    if (path_.active()) send_find(path_.request(eq_time()), false);
}

void GameConnection::find_target(const std::string& name) {
    // A client extension: RoF2's /find (Cmd_find @0x4f4250) is the GM command OP_GMFind. Here it finds a path as
    // FUN_0059da00 does (GetSpawnByName, then PlayerPath_FindSpawn: the player faces the path), to the named spawn
    // (its name with or without EQEmu's number, any case) or else the target.
    const game::Spawn* me = world_.player();
    if (state_ != ConnectionState::InZone || !me) return;
    auto lower = [](std::string t) {
        for (char& c : t) c = c == ' ' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return t;
    };
    std::uint32_t id = world_.target_id;
    if (!name.empty()) {
        id = 0;
        std::string want = lower(name);
        for (const auto& [sid, sp] : world_.spawns) {
            std::string n = lower(sp.name), b = n;
            while (!b.empty() && std::isdigit(static_cast<unsigned char>(b.back()))) b.pop_back();
            if (n == want || b == want) {
                id = sid;
                break;
            }
        }
    }
    if (!id || id == world_.player_id) {
        stop_find_path();
        return;
    }
    send_find(path_.find_spawn(static_cast<std::uint16_t>(id), true, eq_time()));
}

void GameConnection::send_find_path(const game::Spawn& to, bool face) {
    // PlayerPath_FindGroupMember / FindRaidMember (after their checks; they leave +0x15 at 0) and
    // PlayerPath_FindSpawnByID(id, face): the server's path to a spawn.
    if (!world_.player()) return;
    send_find(path_.find_spawn(static_cast<std::uint16_t>(to.id), face, eq_time()));
}

void GameConnection::find_map_location(const glm::vec3& at) {
    // MapViewMap::HandleRButtonDown @0x6c9680: PlayerPath_FindMapLocation(point, 1). EQEmu answers a request without
    // a spawn with an empty reply (the find fails).
    if (state_ != ConnectionState::InZone || !world_.player()) return;
    send_find(path_.find_map_location(at, true, eq_time()));
}

void GameConnection::find_real_estate_plot(std::uint32_t plot) {
    // CRealEstatePlotSearchWnd's Go with mode 0: PlayerPath_FindRealEstatePlot @0x59c2c0 (/find kind 4, the plot at
    // +0x34). EQEmu decodes only the request's npc id, so a stock server fails it (eqstr 6779).
    if (state_ != ConnectionState::InZone || !world_.player()) return;
    send_find(path_.find_plot(static_cast<std::int32_t>(plot), true, eq_time()));
}

void GameConnection::stop_find_path() {
    play_wave(game::kPathSoundAbort);
    path_.stop();
}

void GameConnection::request_quiet_path(std::uint32_t spawn_id) {
    const game::Spawn* me = world_.player();
    if (state_ != ConnectionState::InZone || !me || !spawn_id || spawn_id > 0xffff) return;
    FindPersonRequest fr;
    fr.kind = static_cast<std::uint32_t>(game::FindKind::Spawn);
    fr.id = static_cast<std::uint16_t>(spawn_id);
    fr.at = motion_known_ ? position_ : me->position;
    quiet_path_pending_ = true;
    quiet_path_.reset();
    send_zone(encode(fr));
}

void GameConnection::path_reply(const AppPacket& p) {
    auto points = decode_find_person_reply(p);
    if (!points) return;
    if (quiet_path_pending_) {
        // The navigation's request (request_quiet_path): no find shown, no sound, no message.
        quiet_path_pending_ = false;
        quiet_path_ = std::move(*points);
        return;
    }
    ++find_replies_; // FindLocationWnd closes
    const game::Spawn* me = world_.player();
    auto r = path_.handle_reply(*points, eq_time(), me && me->gm, path_floor_);
    if (r.failed) {
        if (r.failed_point) failed_find_ = FailedFind{*r.failed_point, r.failed_kind};
        system(game::kStrPathPrevented, {}, 0x111);
        return;
    }
    play_wave(game::kPathSoundStart);
    system(game::kStrPathAppears, {}, 0x111);
    if (r.face && me) {
        // PlayerZoneClient_FaceLocation @0x59e7f0: the heading toward the point (__get_bearing).
        glm::vec3 at = motion_known_ ? position_ : me->position;
        landing_.heading = static_cast<float>(game::x87_bearing(at.x, at.y, r.face->x, r.face->y, game::mechanics::Fpu{}));
    }
}

void GameConnection::path_tick() {
    const game::Spawn* me = world_.player();
    if (state_ != ConnectionState::InZone || !me) return;
    auto u = path_.update(motion_known_ ? position_ : me->position, eq_time(), zone_id(), me->gm, path_floor_);
    if (u.arrived) {
        // PlayerPath_OnDestinationReached: the finish sound, "The mystical path fades away." and the finish emitters.
        play_wave(game::kPathSoundComplete);
        system(game::kStrPathFades, {}, 0x111);
        path_finish_ = u.finish_at;
    }
}

} // namespace nmsc::net
