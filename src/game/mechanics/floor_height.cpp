#include "game/mechanics/floor_height.h"

#include <cmath>
#include <cstring>

namespace hailstone::game::mechanics {

namespace {

constexpr float kTenth = 0.1f;      // 0x9c4a48
constexpr float kAbove = 0.001f;    // 0x9c4be8
constexpr float kBottom = -32767.0f; // 0x9c7c0c

float tf(float v) {
    std::uint32_t b;
    std::memcpy(&b, &v, 4);
    if ((b & 0x7f800000u) == 0x7f800000u && (b & 0x007fffffu)) b |= 0x00400000u;
    std::memcpy(&v, &b, 4);
    return v;
}
double st(float v) { return static_cast<double>(tf(v)); }

// Equal (not NaN).
bool equal(double a, double b) { return !std::isnan(a) && !std::isnan(b) && a == b; }

// The floor from the hit or `value`, then the lowest-floor check (StoreFloor). Returns whether it was kept.
bool store_floor(double v, const FloorWorld& w, float& store, double& out) {
    double u = static_cast<double>(w.underworld);
    bool keep = std::isnan(u) || std::isnan(v) || u <= v; // below, equal or unordered keeps
    if (!keep && !w.below_allowed) {
        store = kNoFloorHeight;
        out = static_cast<double>(store);
        return false;
    }
    store = static_cast<float>(v);
    out = v;
    return true;
}

} // namespace

double get_player_floor_height(FloorCache& c, const std::array<float, 3>& p, bool clear, float avatar_height,
                               const FloorWorld& w, FloorQueries& q, const Fpu& f) {
    double out = 0.0;
    if (!q.is_3d()) {
        store_floor(0.0, w, c.floor, out); // the radius (0.0)
        return static_cast<double>(c.floor);
    }
    // FloorRadius: height * 0.1, or 0.1 when that is exactly 0.
    double r = x87_mul(f, st(avatar_height), X87(kTenth));
    float radius = static_cast<float>(r);
    if (equal(0.0, r)) {
        radius = kTenth;
        r = kTenth;
    }
    // CachedFloorHolds.
    bool holds = equal(st(p[0]), st(c.location[0])) && equal(st(p[1]), st(c.location[1]));
    if (holds) {
        double s = x87_add(f, static_cast<double>(kAbove), st(p[2]));
        double cz = st(c.location[2]);
        bool below = !std::isnan(s) && !std::isnan(cz) && s < cz;     // below, ordered
        holds = !below;
    }
    bool again = !holds;
    if (!again) {
        if (w.controlled) {
            // CeilingBelowHead: (ceiling - 2r) < z, r unrounded (0.1 when zero).
            double top = x87_sub(f, X87(c.ceiling), x87_add(f, r, r));
            double z = st(p[2]);
            if (!std::isnan(top) && !std::isnan(z) && top < z) again = true;
        }
        if (!again && q.has_vehicle()) again = true;
    }
    if (again) {
        c.collision_counter = 0;
        c.location = {tf(p[0]), tf(p[1]), tf(p[2])};
    }
    if (c.collision_counter > 2) return static_cast<double>(c.floor);
    std::array<float, 3> from{tf(p[0]), tf(p[1]), tf(p[2])};
    if (!q.scene_accepts(from)) {
        c.floor = kNoFloorHeight;
        return static_cast<double>(c.floor);
    }
    std::array<float, 3> to{tf(p[0]), tf(p[1]), kBottom};
    if (clear) {
        c.touching_switch = false;
        q.touching_cleared();
    }
    q.set_vehicle_none();
    float hit_z = 0.0f;
    if (q.sweep(from, to, radius, clear, hit_z) != 0) {
        c.floor = kNoFloorHeight;
        return static_cast<double>(c.floor);
    }
    // StoreFloor: hit z - radius + 0.001 (unrounded).
    double v = x87_add(f, x87_sub(f, st(hit_z), X87(radius)), X87(kAbove));
    if (store_floor(v, w, c.floor, out)) return v;
    return static_cast<double>(c.floor);
}

} // namespace hailstone::game::mechanics
