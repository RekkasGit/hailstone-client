#include "game/mechanics/can_i_fit_here.h"

#include <cmath>
#include <cstring>

namespace nmsc::game::mechanics {

namespace {

constexpr float kAbove = 0.001f;  // 0x9c4be8
constexpr float kSame = 0.01f;    // 0x9c4be4
constexpr float kOne = 1.0f;      // 0x9c3390
constexpr float kThree = 3.0f;    // 0x9c3920
constexpr float kHalf = 0.5f;     // 0x9c4998
constexpr std::uint8_t kDucking = 0x6f;
constexpr std::uint8_t kStanding = 0x64;

// fld: a float on the stack (a signalling NaN comes out quiet).
double st(float v) {
    std::uint32_t b;
    std::memcpy(&b, &v, 4);
    if ((b & 0x7f800000u) == 0x7f800000u && (b & 0x007fffffu)) b |= 0x00400000u;
    std::memcpy(&v, &b, 4);
    return static_cast<double>(v);
}
// fld / fstp dword: a float copied through the FPU (x87::CopyFloat; a signalling NaN comes out quiet).
float copy(float v) { return static_cast<float>(st(v)); }
// fst dword: rounded to a float.
float to_f(double v) { return static_cast<float>(v); }
// fucom: equal (not unordered).
bool equal(double a, double b) { return !std::isnan(a) && !std::isnan(b) && a == b; }
// fcom: a < b (not unordered).
bool less(double a, double b) { return !std::isnan(a) && !std::isnan(b) && a < b; }

// The refusal: CachedFloorHeight put back, CollisionCounter reset.
void refuse(FloorCache& c, float old_floor) {
    c.floor = copy(old_floor);
    c.collision_counter = 0;
}

// PlayerClient_GetCollisionHeight @0x506340, unrounded.
double collision_height(const FitSpawn& s, const Fpu& f) {
    if (s.stand_state == kDucking) return st(s.avatar_height);
    if (s.mount_race && s.has_rider) return x87_mul(f, st(s.avatar_height), X87(kThree));
    if (0.0f < s.special_height) return st(s.special_height);
    return x87_add(f, st(s.avatar_height), st(s.avatar_height));
}

} // namespace

bool can_i_fit_here(FloorCache& c, const std::array<float, 3>& p, const FitSpawn& s, bool below_allowed,
                    AutoDuck& duck, std::uint32_t display_time, FitQueries& q, const Fpu& f) {
    if (!s.controlled || !s.terrain_collision) return true;
    float radius = s.actor_radius ? to_f(*s.actor_radius) : 1.0f;
    float old_floor = c.floor;

    // The floor, rounded and in full.
    double floor_ext = q.floor(p);
    float floor = to_f(floor_ext);
    if (equal(floor_ext, static_cast<double>(kNoFloorHeight)) && !below_allowed) {
        refuse(c, old_floor);
        return false;
    }

    // The ceiling cache location (@0x5096e5).
    bool stale = !equal(st(p[0]), st(c.ceiling_location[0])) || !equal(st(p[1]), st(c.ceiling_location[1]));
    if (!stale) {
        double z = x87_add(f, st(p[2]), X87(kAbove));
        if (less(z, st(c.ceiling_location[2]))) {
            stale = true;
        } else {
            double top = x87_sub(f, st(c.ceiling), X87(radius));
            if (less(top, z)) {
                stale = true;
            } else {
                double moved = std::fabs(x87_sub(f, st(old_floor), floor_ext)); // fsubr [old]; fabs
                // fcomp 0.01 / test ah, 0x41 / jne keep: kept when below, equal or unordered.
                stale = !(std::isnan(moved) || moved <= static_cast<double>(kSame));
            }
        }
    }
    if (stale) {
        c.collision_counter = 0;
        for (int k = 0; k < 3; ++k) c.ceiling_location[k] = copy(p[k]);
    }

    // The ceiling (@0x50976b).
    double ceiling_ext = c.collision_counter > 2 ? st(c.ceiling) : q.ceiling(p, radius);
    float ceiling = to_f(ceiling_ext);
    if (equal(ceiling_ext, static_cast<double>(kNoFloorHeight))) {
        refuse(c, old_floor);
        return true;
    }

    float height = s.avatar_height;
    float head = to_f(x87_add(f, x87_add(f, collision_height(s, f), st(floor)), st(height)));

    // The height needed.
    double h;
    if (s.stand_state == kDucking) h = x87_add(f, st(s.avatar_height), X87(kOne));
    else if (s.mount_race && s.has_rider) h = x87_mul(f, st(s.avatar_height), X87(kThree));
    else if (0.0f < s.special_height) h = st(s.special_height);
    else h = x87_add(f, st(s.avatar_height), st(s.avatar_height));
    height = to_f(h);
    double top = x87_add(f, x87_add(f, h, st(floor)), X87(kHalf));
    double cv = st(ceiling);
    // Above F + h + 0.5 (or unordered) clears; else at or below the floor (or unordered) clears; between refuses.
    bool clear = !(less(cv, top) || equal(cv, top)) || !less(st(floor), cv);
    if (!clear) {
        if (s.stand_state == kStanding && duck.allowed) {
            duck.active = 1;
            duck.until = display_time + 2000u;
            refuse(c, old_floor);
        }
        return false;
    }

    if (duck.active != 0 && display_time > duck.until) {
        double reach = x87_add(f, st(head), X87(radius));
        if (less(reach, cv)) {
            duck.active = 0;
            duck.until = 0;
            q.stand_up();
            h = st(height);
        }
    }
    double room = x87_sub(f, cv, st(floor));
    if (less(room, h)) {
        refuse(c, old_floor);
        return false;
    }
    q.set_player_ceiling(ceiling);
    c.ceiling = ceiling;
    return true;
}

bool must_collide_still(const StillSpawn& s) {
    if (s.jump_animation) return true;
    if (s.controlled && (s.levitate == 2 || s.levitate == 5)) return true;
    if (s.auto_duck != 0) return true;
    if (s.vehicle_race && !equal(st(s.heading), s.actor_heading)) return true;
    return false;
}

} // namespace nmsc::game::mechanics
