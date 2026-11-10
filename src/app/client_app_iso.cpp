// ClientApp: the isometric camera's click to move and movement relative to the camera (docs/isometric.md). A
// Hailstone addition: every walk goes through the navigator (app/navigator.h), which steers only the heading and the
// movement keys, so the movement rules and what the server sees are the original's.

#include "app/client_app_internal.h"

#include "game/click_intent.h"

namespace hailstone::app::client_internal {

namespace {

// How close a walk to a thing goes before its click. Close enough for every click it ends in (a ground item is picked
// up within 20; a door's reach is the player's height + 20 + its radius; a ladder's mount check is the original's).
constexpr float kIsoObjectArrive = 6.0f;
constexpr float kIsoObjectReach = 12.0f;
// The held left button aims the walk again this often, when the ground under the cursor has moved this far.
constexpr double kIsoHoldSeconds = 0.15;
constexpr float kIsoHoldMove = 2.0f;

} // namespace

float ClientApp::iso_spawn_arrive(std::uint32_t id) const {
    // Inside the player's melee range of it (/stick's share of it, Navigator::kStickMeleeShare): near enough for its
    // right click (a merchant's, a banker's, a corpse's loot) whatever its size. 5 when the range is not known.
    const std::optional<float> melee = melee_range_to(id);
    return std::max(4.0f, melee ? *melee * Navigator::kStickMeleeShare : 5.0f);
}

bool ClientApp::iso_click(const glm::vec2& at, const PickTarget& hit, bool right) {
    if (!nav_ || !conn_) return false;
    const auto& world = conn_->world();
    game::IsoClickInput in;
    in.right = right;
    in.placing = conn_->placement().active();
    in.cursor_item = conn_->inventory().cursor() != nullptr;
    std::optional<glm::vec3> ground;
    const game::Spawn* spawn = nullptr;
    switch (hit.kind) {
    case PickTarget::Kind::None:
        ground = world_point_at(at.x, at.y);
        in.target = ground ? game::IsoClickTarget::Ground : game::IsoClickTarget::Nothing;
        break;
    case PickTarget::Kind::Spawn: {
        in.target = game::IsoClickTarget::Spawn;
        auto it = world.spawns.find(hit.id);
        if (it == world.spawns.end()) return false;
        spawn = &it->second;
        in.walk_to_spawn = (spawn->is_npc || spawn->is_corpse) && hit.id != world.player_id;
        const glm::vec3 p = spawn->display_valid ? spawn->display_position : spawn->position;
        in.in_reach = glm::length(glm::vec2(p - feet_)) <= iso_spawn_arrive(hit.id) + 2.0f;
        break;
    }
    case PickTarget::Kind::Door:
        in.target = game::IsoClickTarget::Door;
        in.in_reach = door_reachable(static_cast<std::uint8_t>(hit.id));
        break;
    case PickTarget::Kind::Object:
        in.target = game::IsoClickTarget::Object;
        in.in_reach = glm::length(glm::vec2(hit.point - feet_)) <= kIsoObjectReach;
        break;
    case PickTarget::Kind::Ladder:
        in.target = game::IsoClickTarget::Ladder;
        in.in_reach = glm::length(glm::vec2(hit.point - feet_)) <= kIsoObjectReach;
        break;
    case PickTarget::Kind::Placed:
        in.target = game::IsoClickTarget::Placed;
        break;
    }
    const game::IsoClickAction action = game::iso_click_action(in);
    if (action == game::IsoClickAction::Original) return false;
    iso_pending_.reset();
    std::string why;
    if (action == game::IsoClickAction::WalkTo) {
        why = nav_->walk_to_click(*ground, 2.0f);
        iso_hold_ = !right;
        iso_hold_point_ = *ground;
        iso_hold_at_ = Clock::now();
    } else if (hit.kind == PickTarget::Kind::Door) {
        // The navigator clicks the door itself once it is within reach (Navigator::go_to_door).
        why = nav_->go_to_door(static_cast<std::uint8_t>(hit.id), true);
    } else if (hit.kind == PickTarget::Kind::Spawn) {
        why = nav_->go_to_spawn(hit.id, iso_spawn_arrive(hit.id));
        if (why.empty()) iso_pending_ = IsoPending{hit, right};
    } else {
        why = nav_->walk_to_click(hit.point, kIsoObjectArrive);
        if (why.empty()) iso_pending_ = IsoPending{hit, right};
    }
    if (!why.empty()) {
        conn_->show_chat("Cannot walk there: " + why + ".", 15);
        iso_hold_ = false;
    } else {
        iso_walking_ = true;
    }
    return true;
}

void ClientApp::update_iso_clicks(bool player_moved) {
    if (!nav_ || !conn_) return;
    const nav::NavStatus status = nav_->status();
    // The player's own movement keys end a click's walk, as they end /nav's, but quietly (each click would say so).
    if (iso_walking_ && player_moved && status.active) {
        nav_->stop("");
        iso_pending_.reset();
        iso_hold_ = false;
    }
    if (!mouse_left_held_ || !iso()) iso_hold_ = false;
    // The left button held on the ground: the walk follows the cursor (not while the mesh is still building, which
    // would ask again and again).
    if (iso_hold_ && status.active && status.mesh_complete && !ui_.wants_mouse() &&
        std::chrono::duration<double>(Clock::now() - iso_hold_at_).count() >= kIsoHoldSeconds) {
        const auto& in = window_.input();
        if (auto p = world_point_at(in.mouse_x, in.mouse_y); p && glm::distance(*p, iso_hold_point_) >= kIsoHoldMove) {
            iso_hold_at_ = Clock::now();
            iso_hold_point_ = *p;
            nav_->walk_to_click(*p, 2.0f);
        }
    }
    if (nav_->status().active) return;
    iso_walking_ = false;
    // The walk is over: arrived, its click; stopped short (keys, no path, stuck), nothing.
    if (!iso_pending_) return;
    const IsoPending pending = *std::exchange(iso_pending_, std::nullopt);
    const PickTarget& hit = pending.hit;
    if (hit.kind == PickTarget::Kind::Spawn) {
        const auto& spawns = conn_->world().spawns;
        auto it = spawns.find(hit.id);
        if (it == spawns.end()) return;
        const glm::vec3 p = it->second.display_valid ? it->second.display_position : it->second.position;
        if (glm::length(glm::vec2(p - feet_)) > iso_spawn_arrive(hit.id) + 2.0f) return;
        // Facing it, as a player walking up to it would be.
        yaw_ = game::heading_to_yaw(game::heading_to(feet_, p));
        if (pending.right) right_click_spawn(hit.id);
        else left_click_spawn(hit.id);
        return;
    }
    if (glm::length(glm::vec2(hit.point - feet_)) > kIsoObjectReach) return;
    click_world(hit);
}

std::optional<bool> ClientApp::iso_key_override() {
    if (!iso() || !iso_wasd_camera_) return std::nullopt;
    const auto& k = movement_.controller().held_keys();
    if (!(k.forward || k.backward || k.strafe_left || k.strafe_right || k.turn_left || k.turn_right))
        return std::nullopt;
    // The gamepad's stick by its angle; the keys by the eight ways they make.
    glm::vec2 v = pad_iso_stick_;
    if (v == glm::vec2(0.0f))
        v = {static_cast<float>(k.strafe_right) - static_cast<float>(k.strafe_left),
             static_cast<float>(k.forward) - static_cast<float>(k.backward)};
    const game::IsoMove m = game::iso_move(v.y, v.x, iso_camera_.yaw);
    if (m.move) yaw_ = m.yaw;
    return m.move;
}

void ClientApp::iso_turn_keys(float dt) {
    if (!iso() || !iso_wasd_camera_) return;
    const auto& k = movement_.controller().held_keys();
    const float turn = static_cast<float>(k.turn_left) - static_cast<float>(k.turn_right);
    if (turn != 0.0f) game::iso_turn(iso_camera_, turn * game::kIsoTurnRate * dt);
}

} // namespace hailstone::app::client_internal
