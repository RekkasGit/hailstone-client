#include "game/collision_callbacks.h"

#include <cmath>

namespace hailstone::game {

namespace {

// The part both filters share for an actor whose owner is not a spawn.
std::uint32_t filter_other(const FilterActor& a) {
    if (a.type == kActorSwitch) {
        if (!a.switch_found) return 1;
        return a.switch_type == 0x32 || a.switch_type == 0x33 ? 0 : 1;
    }
    if (a.type == kActorPlacedObject) return a.object9_found && a.object9_98 ? 0 : 1;
    return 1;
}

// A corpse or an NPC: tested only when its vfa8 or vf50 is set.
std::uint32_t filter_body(const FilterActor& a) { return a.spawn_vfa8 || a.spawn_vf50 ? 1 : 0; }

} // namespace

std::uint32_t collision_filter_movement(const FilterActor& a, bool with_rider) {
    if (!a.has_owner) return 1;
    if (a.type != kActorPlayer) return filter_other(a);
    if (!a.spawn_found) return 1;
    bool body = a.spawn_is_corpse || a.spawn_type == 1;
    if (!with_rider) return body ? filter_body(a) : 1;
    if (body) return filter_body(a);
    if (!a.mover_found) return 1;
    return a.spawn_is_movers_rider ? 0 : 1;
}

HitResult collision_hit_set_colliding_type(const HitActor& a, bool acts_on_touch) {
    HitResult r;
    if (a.flags & 4) return r;
    if (!a.collisions_enabled || a.is_camera_actor) return r;
    r.colliding_type = a.type; // the original writes it before telling the cases apart
    if (!a.has_owner) {
        r.ret = 1;
        return r;
    }
    if (a.type == kActorMissile) return r; // the sweep goes on through missiles
    if (a.type == kActorSwitch && a.door_has_switch) {
        r.asked_touch = true;
        r.touching_switch = acts_on_touch;
    }
    r.ret = 1;
    return r;
}

bool switch_acts_on_touch(std::uint8_t t) {
    switch (t) {
    case 0x33: case 0x73: case 0x74: case 0x78: case 0x7d: case 0x82: case 0x87: case 0x8c: case 0x91: case 0x92:
    case 0x96: case 0x97: case 0x98:
        return true;
    default:
        return false;
    }
}

} // namespace hailstone::game

namespace hailstone::game {

std::uint32_t floor_actor_filter(const FloorFilterActor& a, bool is_floor_switch, bool* asked) {
    if (asked) *asked = false;
    int calls = 0;
    auto vf30 = [&] { return calls++ == 0 ? a.type : a.type_again; };
    if (a.mover_present && vf30() == kActorPlacedObject) {
        if (a.owner_is_mover) return 0;
        if (a.mover_vf24) return 1;
    }
    if (a.restriction_mask & 1) return 0;
    if (a.has_owner) {
        std::int32_t type = vf30();
        if (type == kActorPlayer) {
            if (a.mover_spawn_found && (a.is_own_actor || a.is_mount_actor || a.is_rider_actor)) return 0;
        } else if (type == kActorMissile) {
            return 0;
        } else if (type == kActorSwitch) {
            if (asked) *asked = true;
            return is_floor_switch ? 1 : 0;
        }
    }
    if (a.volume_type == 0 || a.volume_type == 1) return 1;
    return a.volume_radius == 0.0 ? 1 : 0; // fldz; fucompp; not NaN
}

FloorHitResult collsn_callback_for_floor(const FloorHit& h, const mechanics::Fpu& f) {
    FloorHitResult r;
    // fld height; fadd 2.0; fcomp distance; sete: height + 2.0 > distance.
    auto within = [&] {
        double s = mechanics::x87_add(f, mechanics::X87(h.player_height), mechanics::X87(2.0f));
        float d = h.distance;
        return !std::isnan(s) && !std::isnan(d) && s > static_cast<double>(d);
    };
    if (!h.is_camera_actor) {
        r.ret = 1;
        if (h.player_found) {
            if (h.actor_is_player) {
                r.ret = 0;
            } else if (h.has_owner) {
                if (h.type == kActorSwitch) {
                    if (!h.switch_present) return {0, false, false};
                    std::uint8_t t = h.switch_type;
                    if (t == 0x46) {
                        if (h.zone != 0x3b) r.ret = 0;
                        else if (within()) r.touching_switch = true;
                    } else if (t <= 7 || (t >= 0x3c && t <= 0x4d)) {
                        r.ret = 0;
                    } else {
                        if (within()) r.touching_switch = true;
                        if ((t >= 0x73 && t <= 0x98) || t == 0x32 || t == 0x33) r.ret = 0;
                    }
                } else if (h.type_again == kActorPlayer) {
                    if (!h.hit_spawn_is_player) {
                        if (!h.hit_spawn_rideable) r.ret = 0;
                        else r.set_vehicle = true;
                    }
                }
            }
        }
    }
    if (h.actor_null) r.ret = 1;
    return r;
}

} // namespace hailstone::game
