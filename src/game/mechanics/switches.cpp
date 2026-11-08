#include "game/mechanics/switches.h"

#include <cmath>
#include <cstring>

// Values "on the stack" are doubles (quiet); a float in memory goes in as
// X87(float). Operands are passed in the order the instruction combines them.

namespace hailstone::game::mechanics {

namespace {

constexpr float kFullTurn = 512.0f;     // 0x9c7c68
constexpr float kHalfTurn = 256.0f;     // 0x9c7328
constexpr float kTickScale = 0.02f;     // 0x9c58fc
constexpr float kMaxTicks = 15.0f;      // 0x9c588c
constexpr float kDefaultRadius = 100.0f; // 0x9c3988
constexpr float kSpawnRadius = 10.0f;   // 0x9c5b28
constexpr float kRiderReach = 20.0f;    // 0xac1a6c
constexpr float kTwoTo32 = 4294967296.0f; // 0x9c3eac
constexpr std::uint32_t kTriggeredMs = 60000;

float tf(float v) { // fld / fstp dword
    std::uint32_t b;
    std::memcpy(&b, &v, 4);
    if ((b & 0x7f800000u) == 0x7f800000u && (b & 0x007fffffu)) b |= 0x00400000u;
    std::memcpy(&v, &b, 4);
    return v;
}
double st(float v) { return static_cast<double>(tf(v)); }
float to_f(double v) { return static_cast<float>(v); }

// The x87 compare of a with b: below, equal, unordered.
struct Cmp {
    bool c0, c3;
};
Cmp cmp(double a, double b) {
    if (std::isnan(a) || std::isnan(b)) return {true, true};
    return {a < b, a == b};
}

// fld dt; fmul speed; fadd angle (add) or fsubr angle; fstp.
float turned(const Fpu& f, float dt, float speed, float angle, bool add) {
    double p = x87_mul(f, st(dt), X87(speed));
    return to_f(add ? x87_add(f, p, X87(angle)) : x87_sub(f, X87(angle), p));
}

// __HeadingDiff(a, b) then fld limit; fcom st(1); test ah, 0x41; setnp: limit <= diff (not unordered).
bool turn_done(const Fpu& f, float a, float b, float limit) {
    Cmp c = cmp(static_cast<double>(limit), heading_diff_x87(tf(a), tf(b), f));
    return c.c0 != c.c3;
}

// The lifts' and platforms' step: a coordinate *c between its base b and b + travel (see the rewrite's
// StepToward). Returns true when done.
bool step_toward(const Fpu& f, float& c, float b, double travel, double open, double close, float dt, bool opening) {
    double d = travel;
    double t = x87_add(f, st(b), d); // fld D; fld b; fadd st, st(1)
    Cmp z = cmp(0.0, d);             // fldz; fcomp st(2)
    if (!(z.c0 || z.c3)) d = -d;     // not (0 < D, equal, unordered): |D|
    if (opening) {
        double p = x87_mul(f, open, X87(dt));          // fld open; fmul dt
        double v = x87_add(f, p, X87(c));              // fadd c
        c = to_f(v);                                   // fst c
        double diff = std::fabs(x87_sub(f, v, X87(b))); // fsub b; fabs
        if (cmp(diff, d).c0) return false;             // fcomp st(2); test ah, 1: below or unordered
        c = to_f(t);
        return true;
    }
    double p = x87_mul(f, close, X87(dt)); // fld close; fmul dt
    double v = x87_sub(f, X87(c), p);      // fsubr c
    c = to_f(v);                           // fst c
    double diff = std::fabs(x87_sub(f, v, t)); // fsubrp st(1): v - T
    if (cmp(diff, d).c0) return false;
    c = tf(b);
    return true;
}

// The swinging switches (0x48d927..0x48dae1). Returns 0, or 1 + the state to change to.
std::uint32_t swing(const Fpu& f, Switch& sw, float& angle, float base, float ticks) {
    double s = st(sw.spin);
    double a = st(sw.spin_accel);
    if (sw.state == 2) {
        sw.swing_speed = to_f(x87_add(f, st(sw.swing_speed), a)); // fld v; fadd st, st(1); fstp v
        if (sw.swing_stop) sw.swing_speed = to_f(s);              // fst v (the spin)
        Cmp za = cmp(0.0, a);
        bool parity_even = za.c0 == za.c3; // 0 > a, or unordered
        if (!parity_even) {
            Cmp sv = cmp(s, static_cast<double>(sw.swing_speed)); // fcom [v]
            if (sv.c0 != sv.c3) return 2;                          // s <= v: open
        }
        // fcompp: 0 vs a.
        if (za.c0 || za.c3) {
            // 0 <= a or unordered: move.
        } else {
            Cmp sv = cmp(s, static_cast<double>(sw.swing_speed)); // fcomp [v]
            if (!sv.c0) return 2;                                  // not below (and ordered): open
        }
        angle = to_f(x87_add(f, x87_mul(f, st(sw.swing_speed), X87(ticks)), X87(angle)));
        return 0;
    }
    if (sw.state == 1) {
        angle = to_f(x87_add(f, x87_mul(f, s, X87(ticks)), X87(angle)));
        return sw.swing_stop ? 4 : 0;
    }
    // Closing (and any other state).
    double v = x87_sub(f, st(sw.swing_speed), a); // fld v; fsub st, st(1)
    sw.swing_speed = to_f(v);                      // fst v
    if (sw.swing_stop) {
        double w = x87_add(f, a, v);                   // faddp st(1)
        sw.swing_speed = to_f(w);                      // fst v
        double p = x87_mul(f, w, X87(ticks));          // fmul ticks
        double n = x87_add(f, st(angle), p);           // fld angle; fadd st, st(1)
        Cmp zp = cmp(0.0, p);                          // fldz; fcom st(3)
        double b = st(base);
        Cmp ba = cmp(b, static_cast<double>(angle));   // fld base; fcomp [angle]
        bool reached;
        if (!(zp.c0 || zp.c3)) {
            // p < 0.
            if (!(ba.c0 || ba.c3)) b = x87_sub(f, b, X87(kFullTurn)); // base > angle: - 512
            Cmp bn = cmp(b, n);
            reached = !bn.c0; // not below (and ordered)
        } else {
            if (ba.c0 != ba.c3 && ba.c0) b = x87_add(f, b, X87(kFullTurn)); // base < angle: + 512
            Cmp bn = cmp(b, n);
            reached = bn.c0 != bn.c3; // below or equal
        }
        if (reached) {
            sw.swing_stop = false;
            angle = tf(base);
            sw.swing_speed = 0.0f;
            return 0;
        }
        angle = to_f(n);
        return 0;
    }
    Cmp za = cmp(0.0, a);
    if (za.c0 != za.c3) { // 0 < a or 0 == a
        Cmp zv = cmp(0.0, v);
        if (!zv.c0) return 1; // 0 >= v: closed
    }
    // La99: 0 vs a.
    if (za.c0 || za.c3) {
        // 0 <= a or unordered: move.
    } else {
        Cmp vz = cmp(v, 0.0);
        if (!(vz.c0 || vz.c3)) return 1; // v > 0: closed
    }
    angle = to_f(x87_add(f, x87_mul(f, v, X87(ticks)), X87(angle)));
    return 0;
}

struct Lift {
    double travel, open;
    float close;
    bool finish;
};

Lift lift_for(Switch& sw, float dt, SwitchQueries& q) {
    const float slow = 0.25f, lift = 0.45f;
    Lift l{128.0, slow, slow, true};
    switch (sw.type) {
    case 0x4e: case 0x4f: case 0x50: case 0x51: {
        l.finish = q.slide_switch_left_right(sw, 0, dt) != 0;
        double g = custom_move_distance_x87(sw.custom);
        bool down = sw.type >= 0x50;
        l.travel = down ? -g : g;
        l.open = down ? -0.45f : lift;
        l.close = static_cast<float>(l.open);
        break;
    }
    case 0x3b:
        l.travel = custom_move_distance_x87(sw.custom);
        l.open = lift;
        l.close = lift;
        break;
    case 0x41: case 0x42: case 0x43: l.travel = 10.0; l.open = lift; break;
    case 0x3c: case 0x3d: case 0x3e: l.travel = 8.0; l.open = lift; break;
    case 0x46: case 0x47: case 0x48: l.travel = 14.0; l.open = lift; break;
    case 0x4b: case 0x4c: case 0x4d: {
        double g = custom_move_distance_x87(sw.custom);
        l.travel = g == 0.0 ? 22.0 : g;
        l.open = lift;
        break;
    }
    case 0x52: case 0x53: case 0x54: {
        std::int32_t v = sw.custom;
        l.travel = v != 0 ? static_cast<double>(v) : 22.0;
        l.open = v < 0 ? -0.45f : lift;
        l.close = static_cast<float>(l.open);
        break;
    }
    case 0x91: l.travel = -10.0; l.open = -0.25f; l.close = -0.25f; break;
    case 0x92: l.travel = -10.0; l.open = -1.0f; l.close = -1.0f; break;
    case 0x78: case 0x8c: l.travel = -10.0; l.open = -1.0f; l.close = -0.25f; break;
    default: break;
    }
    return l;
}

bool is_lift(std::uint8_t t) {
    return (t >= 0x3c && t <= 0x4d) || (t >= 0x8c && t <= 0x92) || t == 0x78 || t == 0x3b || (t >= 0x4e && t <= 0x54);
}

// The first stage of a switch's movement (0x48d747..0x48df20).
void move_switch(const Fpu& f, Switch& sw, float dt, SwitchQueries& q) {
    std::uint8_t type = sw.type;
    float limit = 128.0f;
    if (type <= 8) {
        float speed = 8.0f;
        if (type >= 5 && type <= 7) {
            speed = -8.0f;
            limit = -128.0f;
        }
        float target = to_f(fix_heading_x87(to_f(x87_add(f, st(limit), X87(sw.base_heading))), f));
        if (limit < 0.0f) limit = -limit;
        bool opening = sw.state == 2;
        sw.heading = turned(f, dt, speed, sw.heading, opening);
        float h = to_f(fix_heading_x87(sw.heading, f));
        sw.heading = h;
        if (opening) {
            if (turn_done(f, h, sw.base_heading, limit)) {
                sw.heading = tf(target);
                q.change_state(sw, 1);
            }
        } else if (turn_done(f, h, target, limit)) {
            sw.heading = tf(sw.base_heading);
            q.change_state(sw, 0);
        }
        return;
    }
    if (type == 0x28 || type == 0x1e || type == 0x23 || type == 0x82 || type == 0x24) {
        float speed = 8.0f;
        float turn = limit;
        switch (type) {
        case 0x1e:
            if (0.0f < sw.spin) speed = tf(sw.spin);
            else speed = 10.0f;
            break;
        case 0x24: speed = -16.0f; break;
        case 0x23: speed = 16.0f; break;
        case 0x82:
            speed = 16.0f;
            limit = 248.0f;
            turn = limit;
            break;
        default: break;
        }
        float target = to_f(fix_heading_x87(to_f(x87_sub(f, X87(sw.base_pitch), st(turn))), f));
        if (limit < 0.0f) limit = -limit;
        if (sw.state == 2) {
            if (turn_done(f, sw.pitch, sw.base_pitch, limit)) {
                sw.pitch = tf(target);
                q.change_state(sw, 1);
            } else {
                sw.pitch = to_f(fix_heading_x87(turned(f, dt, speed, sw.pitch, false), f));
            }
        } else if (sw.state == 3) {
            if (turn_done(f, sw.pitch, target, limit)) {
                sw.pitch = tf(sw.base_pitch);
                q.change_state(sw, 0);
            } else {
                sw.pitch = to_f(fix_heading_x87(turned(f, dt, speed, sw.pitch, true), f));
            }
        }
        return;
    }
    if (is_lift(type)) {
        Lift l = lift_for(sw, dt, q);
        double close = l.close;
        if (sw.state == 2) {
            if (step_toward(f, sw.position[2], sw.base[2], l.travel, l.open, close, dt, true)) q.change_state(sw, 1);
        } else if (sw.state == 3) {
            if (step_toward(f, sw.position[2], sw.base[2], l.travel, l.open, close, dt, false) && l.finish)
                q.change_state(sw, 0);
        }
        return;
    }
    if (type >= 0xa && type <= 0x1b) {
        q.slide_switch_left_right(sw, 1, dt);
        return;
    }
    if (type == 0x9c || type == 0x9d || type == 0x73 || type == 0x74) {
        bool head = type == 0x9c;
        float& angle = head ? sw.heading : sw.pitch;
        float base = head ? sw.base_heading : sw.base_pitch;
        std::uint32_t code = swing(f, sw, angle, base, dt);
        if (code) q.change_state(sw, code - 1);
        angle = to_f(fix_heading_x87(angle, f));
    }
}

// The second stage (0x48df22..0x48e11e): platforms sliding along x or y by their heading.
void slide_switch(const Fpu& f, Switch& sw, float dt, SwitchQueries& q) {
    double open, close, travel;
    switch (sw.type) {
    case 0x2d: case 0x6d: open = close = -0.05f; travel = -1.0; break;
    case 0x7d: open = 0.75f; close = 0.35f; travel = 10.0; break;
    case 0x96: open = 1.0; close = 0.25f; travel = 10.0; break;
    case 0x97: open = close = 0.25f; travel = 10.0; break;
    case 0x98: open = close = 1.0; travel = 10.0; break;
    case 0x73: open = close = 0.75f; travel = 20.0; break;
    case 0x74: open = close = 0.75f; travel = 40.0; break;
    default: return;
    }
    float h = sw.heading;
    bool along_x;
    if (h == 0.0f) {
        along_x = true;
    } else if (h == 128.0f) {
        along_x = false;
    } else {
        along_x = h == kHalfTurn;
        open = -open;
        close = -close;
        travel = -travel;
    }
    float& c = sw.position[along_x ? 0 : 1];
    float b = sw.base[along_x ? 0 : 1];
    if (sw.state == 2) {
        if (step_toward(f, c, b, travel, open, close, dt, true)) q.change_state(sw, 1);
    } else if (sw.state == 3) {
        if (step_toward(f, c, b, travel, open, close, dt, false)) q.change_state(sw, 0);
    }
}

// 0x48e1d0..0x48e214: the spawn's actor radius (10.0 without one) + R + 20.0 > its x, y distance from (cy, cx).
bool near_switch(const Fpu& f, const SwitchSpawn& s, float radius, float cy, float cx) {
    double r = s.actor_radius ? *s.actor_radius : static_cast<double>(kSpawnRadius);
    double d0 = x87_sub(f, st(s.y), X87(cy));
    double d1 = x87_sub(f, st(s.x), X87(cx));
    double d11 = x87_mul(f, d1, d1);
    double d00 = x87_mul(f, d0, d0);
    double dist = f.sqrt(x87_add(f, d00, d11));
    double reach = x87_add(f, x87_add(f, r, X87(radius)), X87(kRiderReach));
    Cmp c = cmp(reach, dist);
    return !c.c0 && !c.c3;
}

} // namespace

double fix_heading_x87(float heading, const Fpu& f) {
    double h = st(heading);
    if (!cmp(h, kFullTurn).c0) { // not below 512 (and ordered): subtract, at most six times
        for (int i = 0;; ++i) {
            h = x87_sub(f, h, static_cast<double>(kFullTurn)); // fsub st, st(2)
            if (i > 5) return 0.0;
            if (cmp(h, kFullTurn).c0) break;
        }
    }
    Cmp z = cmp(0.0, h);
    if (z.c0 || z.c3) return h; // 0 <= h, or unordered
    for (int i = 0;; ++i) {
        h = x87_add(f, h, static_cast<double>(kFullTurn)); // fadd st, st(2)
        if (i > 5) return 0.0;
        Cmp c = cmp(h, 0.0);
        if (!(c.c0 && !c.c3)) return h; // test ah, 5; jnp: loop only while below (ordered)
    }
}

double heading_diff_x87(float a, float b, const Fpu& f) {
    double x = st(b), y = st(a);       // fld b; fld a
    Cmp c = cmp(y, x);                 // fcom st(1); test ah, 0x41; je: a > b keeps the order
    double d = (!c.c0 && !c.c3) ? x87_sub(f, y, x) : x87_sub(f, x, y); // fsubrp st(1)
    Cmp h = cmp(static_cast<double>(kHalfTurn), d); // fld 256; fcomp st(1); test ah, 5; jp
    if (!(h.c0 && !h.c3)) return d;   // 256 > d, equal or unordered
    return -x87_sub(f, d, X87(kFullTurn)); // fsub 512; fchs
}

double custom_move_distance_x87(std::int32_t v) {
    if (v > 0) return static_cast<double>(v);
    if (v < -3) return -static_cast<double>(v);
    return 0.0;
}

bool switch_is_static(std::uint8_t t) { return (t >= 0x32 && t <= 0x3a && t != 0x35) || (t >= 0x99 && t <= 0x9b); }

void process_switches(std::span<Switch*> switches, std::uint32_t elapsed_ms, std::uint32_t now_ms, SwitchQueries& q,
                      const Fpu& f) {
    // 0x48d55a: the unsigned milliseconds (fild, + 2^32 when negative) * 0.02.
    double e = static_cast<double>(static_cast<std::int32_t>(elapsed_ms));
    if (static_cast<std::int32_t>(elapsed_ms) < 0) e = x87_add(f, e, X87(kTwoTo32));
    float dt = to_f(x87_mul(f, e, X87(kTickScale)));
    float radius = kDefaultRadius;
    float centre[2] = {0.0f, 0.0f}; // the original's uninitialised slots, as the rewrite
    for (Switch* p : switches) {
        if (kMaxTicks < dt) break;
        Switch& sw = *p;
        if (sw.hidden) continue;
        if (sw.type == 0x35) {
            q.update_day_night(sw);
            if (sw.state == 1 && !sw.particle) {
                if (sw.custom > 0 && sw.custom < 0x100 && q.local_player()) sw.particle = q.start_particle(sw, sw.custom);
            } else if (sw.state == 0 && sw.particle) {
                q.stop_particle(sw);
                sw.particle = false;
            }
        }
        if (switch_is_static(sw.type)) continue;
        std::uint32_t elapsed = now_ms - sw.timestamp;
        std::uint8_t s = sw.state;
        if (s != sw.default_state) {
            if (s == 4) {
                if (elapsed <= kTriggeredMs) continue;
                q.change_state(sw, sw.default_state);
            } else {
                std::uint32_t repop = q.repop_frequency(sw);
                if (repop != 0 && elapsed > repop) q.repop_switch(sw);
            }
        }
        std::uint8_t type = sw.type;
        if (type == 0x35 || type == 0x39 || type == 0x3a) {
            if (sw.state == 2) q.change_state(sw, 1);
            else if (sw.state == 3) q.change_state(sw, 0);
            continue;
        }
        if (sw.state == 0 || (sw.state == 1 && type < 0x64)) continue;
        bool touch = false;
        switch (sw.type) { // FUN_00510210
        case 0x33: case 0x73: case 0x74: case 0x78: case 0x7d: case 0x82: case 0x87: case 0x8c: case 0x91: case 0x92:
        case 0x96: case 0x97: case 0x98:
            touch = true;
            break;
        default: break;
        }
        bool floor = q.is_floor_switch(sw);
        if (floor) {
            auto r = q.actor_radius(sw);
            radius = r ? to_f(*r) : kDefaultRadius;
            centre[0] = tf(sw.position[0]);
            centre[1] = tf(sw.position[1]);
        }
        move_switch(f, sw, dt, q);
        slide_switch(f, sw, dt, q);
        if (sw.state == 1 && sw.type != 0x73) {
            if (sw.type == 0x6d) q.repop_switch(sw);
            else if (sw.type > 0x6d && sw.type <= 0x98) q.change_state(sw, 3);
        }
        if (!q.actor_radius(sw)) continue;
        float position[3] = {tf(sw.position[0]), tf(sw.position[1]), tf(sw.position[2])};
        float orientation[3] = {tf(sw.heading), tf(sw.pitch), 0.0f};
        q.actor_place(sw, position, orientation);
        if (!floor && !touch) continue;
        auto spawns = q.spawns();
        for (std::size_t i = 0; i < spawns.size(); ++i) {
            if (!near_switch(f, spawns[i], radius, centre[0], centre[1])) continue;
            if (floor) q.reset_collision_counter(i); // byte 0xac1a68: 1 in the exe, never written
            else q.set_switch_moved(i);
        }
    }
}

} // namespace hailstone::game::mechanics
