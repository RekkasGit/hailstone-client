#include "game/mechanics/collision_movement.h"

#include <cmath>
#include <cstring>

// Names in the comments: F<off> is the original's frame local [esp + off]
// after its prologue (rof2decomp's Frame), so the steps can be read side by
// side with the rewrite. A value "on the stack" is a double here (x87 values
// are quiet); a float in memory goes in as X87(float) and keeps a signalling
// NaN's flag; operands are passed in the order the instruction combines them
// (st(i) op st(0) for the reverse forms).

namespace hailstone::game::mechanics {

namespace {

constexpr float kBoundsMargin = 2000.0f; // 0x9c8264
constexpr float kStill = 0.01f;          // 0x9c4be4
constexpr float kStillLow = -0.01f;      // 0x9c53e4
constexpr float kSlideLimit = 0.1f;      // 0x9c4a48
constexpr float kFlat = 0.001f;          // 0x9c4be8
constexpr float kBarely = 1e-6f;         // 0x9cf850 (a squared distance)
constexpr float kMinusOne = -1.0f;       // 0x9c4ec4
constexpr float kNormalizeLow = -0x1p-23f; // 0x9c338c
constexpr float kNormalizeHigh = 0x1p-23f; // 0x9c3388
constexpr std::uint32_t kPropertyNoCollide = 0x64;

// fld dword / fstp dword: a signalling NaN comes out quiet.
float tf(float v) {
    std::uint32_t b;
    std::memcpy(&b, &v, 4);
    if ((b & 0x7f800000u) == 0x7f800000u && (b & 0x007fffffu)) b |= 0x00400000u;
    std::memcpy(&v, &b, 4);
    return v;
}

// fld dword: the value on the stack.
double st(float v) { return static_cast<double>(tf(v)); }

// fstp dword.
float to_f(double v) { return static_cast<float>(v); }

// The square of a vector's length as the original sums it: with the three
// components loaded in the order (b, a, c) of [+4], [+0], [+8]:
// (b*b + a*a) + c*c, each step rounded (0x509130 and the speed checks).
double length_squared(const Fpu& f, double a, double b, double c) {
    double aa = x87_mul(f, a, a);  // fld st(1); fmulp st(2): a * a
    double bb = x87_mul(f, b, b);  // fld st(2); fmulp st(3): b * b
    double sum = x87_add(f, bb, aa); // fxch; faddp st(2): b*b + a*a
    double cc = x87_mul(f, c, c);  // fmul st, st
    return x87_add(f, sum, cc);    // faddp st(1)
}

struct Frame {
    Vec3f f3c{};          // the actor's location (F3c, F40, F44)
    double lifted_z = 0;  // F44 + height, unrounded (X)
    Vec3f f30{};          // the lifted start (F30, F34, F38)
    Vec3f f4c{};          // the sweep's end (F4c, F50, F54)
    Vec3f f24{};          // the sweep's target (F24, F28, F2c); later a scratch vector
    Vec3f f58{};          // the slide-back / offset vector
    Vec3f f64{};          // the position before the lift (F64, F68, F6c); later a sweep's normal
    Vec3f f74{};          // the skitter direction
    Vec3f f80{};          // the sweep's start
    float f20 = 0.0f;     // the skitter distance
    float f14 = 0.0f;
    float f70 = 0.0f;     // the height
    float f1c = 0.0f, f18 = 0.0f, f94 = 0.0f; // the move
    float f6c = 0.0f;     // |dz| of the move (BarelyMoved)
};

// 0x50817a..0x5081fa: the target strictly inside the box grown by the margin.
bool inside_scene(const Fpu& f, const Vec3f& min, const Vec3f& max, const Vec3f& target) {
    double margin = static_cast<double>(kBoundsMargin);
    double lo[3], hi[3];
    lo[0] = x87_sub(f, st(min[0]), margin); // fsub st(1), st: min.x - margin
    lo[1] = x87_sub(f, st(min[1]), margin); // fsub st, st(1)
    lo[2] = x87_sub(f, st(min[2]), margin);
    hi[0] = x87_add(f, st(max[0]), margin); // fadd st, st(3)
    hi[1] = x87_add(f, st(max[1]), margin);
    hi[2] = x87_add(f, margin, st(max[2])); // faddp st(5), st: margin + max.z
    // fcomp: test ah, 0x41 leaves on below, equal or unordered (needs hi > t);
    // test ah, 5 / jp leaves unless exactly below (needs lo < t).
    for (int k = 0; k < 3; ++k)
        if (!(hi[k] > static_cast<double>(target[k]))) return false;
    for (int k = 0; k < 3; ++k)
        if (!(lo[k] < static_cast<double>(target[k]))) return false;
    return true;
}

// 0x50864f..0x5086d0: the distance from F80 to the hit F4c; strictly between
// 0 and 0.1, F58 = F30 - the hit and true.
bool slide_back(const Fpu& f, Frame& fr) {
    double hx = st(fr.f4c[0]), hy = st(fr.f4c[1]), hz = st(fr.f4c[2]);
    double dx = x87_sub(f, hx, st(fr.f80[0])); // fsubr st, st(1): hit - start
    double dy = x87_sub(f, hy, st(fr.f80[1]));
    double dz = x87_sub(f, hz, st(fr.f80[2]));
    double xx = x87_mul(f, dx, dx); // fld st(4); fmulp st(5)
    double yy = x87_mul(f, dy, dy); // fld st(2); fmulp st(3)
    double s = x87_add(f, yy, xx);  // fxch st(4); faddp st(2): y*y + x*x
    double zz = x87_mul(f, dz, dz); // fld st(3); fmulp st(4)
    double total = x87_add(f, zz, s); // fxch st(1); faddp st(3): z*z + s
    double length = f.sqrt(total);   // fsqrt
    // fld [0.1]; fcomp st(1): leaves when 0.1 <= length or unordered.
    if (!(static_cast<double>(kSlideLimit) > length)) return false;
    // fldz; fcompp: needs 0 < length.
    if (!(0.0 < length)) return false;
    fr.f58[0] = to_f(x87_sub(f, st(fr.f30[0]), hx)); // fsubrp st(3), st: F30 - hit
    fr.f58[1] = to_f(x87_sub(f, st(fr.f30[1]), hy));
    fr.f58[2] = to_f(x87_sub(f, X87(fr.f30[2]), hz)); // fsubr [F38]
    return true;
}


// 0x5086d9..0x5086fd: F74 += F58, each component rounded.
void add_back(const Fpu& f, Frame& fr) {
    fr.f74[0] = to_f(x87_add(f, st(fr.f58[0]), X87(fr.f74[0]))); // fld F58; fadd [F74]
    fr.f74[1] = to_f(x87_add(f, st(fr.f74[1]), X87(fr.f58[1]))); // fld F78; fadd [F5c]
    fr.f74[2] = to_f(x87_add(f, st(fr.f74[2]), X87(fr.f58[2]))); // fld F7c; fadd [F60]
}

// 0x50873b..0x5085bf: the next sweep starts at the hit (F80 = F4c) and aims
// at the hit + F74 * step (F24).
void advance(const Fpu& f, Frame& fr, float step) {
    double s = st(step);
    double hx = st(fr.f4c[0]), hy = st(fr.f4c[1]), hz = st(fr.f4c[2]);
    fr.f80 = {to_f(hx), to_f(hy), to_f(hz)};
    double mx = x87_mul(f, st(fr.f74[0]), s); // fmul st, st(4)
    double my = x87_mul(f, st(fr.f74[1]), s); // fmul st, st(5)
    double mz = x87_mul(f, s, st(fr.f74[2])); // fmulp st(6), st
    fr.f24[0] = to_f(x87_add(f, hx, mx));     // faddp st(4), st
    fr.f24[1] = to_f(x87_add(f, hy, my));     // faddp st(2), st
    fr.f24[2] = to_f(x87_add(f, mz, hz));     // faddp st(3), st
}

// 0x508837..0x508894: F6c = |dz| of the move from F30; true when |dz| is at
// most 0.001 and the squared distance is below 1e-6.
bool barely_moved(const Fpu& f, Frame& fr, const Vec3f& position) {
    double dx = std::fabs(x87_sub(f, st(position[0]), X87(fr.f30[0]))); // fld; fsub [F30]; fabs
    double dy = std::fabs(x87_sub(f, st(position[1]), X87(fr.f30[1])));
    double dz = std::fabs(x87_sub(f, st(position[2]), X87(fr.f30[2])));
    fr.f6c = to_f(dz); // fst
    // fld [0.001]; fcomp st(1); test ah, 1: 0.001 < |dz| (or unordered) moved far.
    if (static_cast<double>(kFlat) < dz || std::isnan(dz)) return false;
    double sum = length_squared(f, dx, dy, dz); // (dy*dy + dx*dx) + dz*dz
    return sum < static_cast<double>(kBarely); // fcomp [1e-6]; test ah, 5; jp
}

} // namespace

void normalize_vector(float v[3], const Fpu& f) {
    double length = f.sqrt(length_squared(f, st(v[0]), st(v[1]), st(v[2])));
    // fld [-2^-23]; fcomp st(1); test ah, 5; jp: on to the zero test only when -2^-23 < length.
    // fld [2^-23]; fcomp st(1); test ah, 0x41; jne: zero only when 2^-23 > length.
    if (static_cast<double>(kNormalizeLow) < length && static_cast<double>(kNormalizeHigh) > length) {
        v[0] = v[1] = v[2] = 0.0f; // fldz; fst [8]; fst [4]; fstp [0]
        return;
    }
    double inverse = x87_div(f, 1.0, length); // fld1; fdivrp st(1), st: 1 / length
    v[0] = to_f(x87_mul(f, st(v[0]), inverse)); // fld [0]; fmul st, st(1)
    v[1] = to_f(x87_mul(f, inverse, X87(v[1]))); // fld st(0); fmul [4]
    v[2] = to_f(x87_mul(f, inverse, X87(v[2]))); // fmul [8]
}

double collision_volume_radius(float avatar_height, bool npc, std::uint32_t race, bool mount_race, const Fpu& f) {
    double r = x87_mul(f, st(avatar_height), X87(0.5f)); // fld [+0x138]; fmul [0.5]
    if (!npc) return x87_mul(f, r, X87(0.95f));         // fmul [0.95]
    float v = to_f(x87_mul(f, r, X87(0.35f)));           // fmul [0.35]; fstp
    if (race == 31) v = to_f(x87_mul(f, st(v), X87(0.9f)));
    double out = st(v);
    if (mount_race) out = x87_add(f, out, out);          // fadd st, st
    return out;
}

double model_sweep_radius(float model_radius, float scale, float co_factor, const Fpu& f) {
    float t = to_f(x87_mul(f, st(scale), X87(model_radius))); // getter +0x60; fmul [model +0x38]; fstp
    return x87_mul(f, st(co_factor), X87(t));                 // getter +0x8c; fmul
}

float sweep_radius(const SweepRadiusSpawn& spawn) {
    // Stand-in: 0x5062a0 for a player, stored as a float (SetCollisionVolumeRadius).
    return static_cast<float>(collision_volume_radius(spawn.avatar_height, false, spawn.race, false));
}

bool keep_in_scene(CollisionSpawn& s, bool collision_loaded, const Vec3f& min, const Vec3f& max, const Fpu& f) {
    Vec3f position{s.physics[0], s.physics[1], s.physics[2]};
    if (collision_loaded && !inside_scene(f, min, max, position)) {
        s.physics[3] = s.physics[4] = s.physics[5] = 0.0f;
        for (int k = 0; k < 3; ++k) s.physics[k] = tf(s.last_good[k]);
        return false;
    }
    for (int k = 0; k < 3; ++k) s.last_good[k] = s.physics[k]; // rep movs: the bits as they are
    return true;
}

CollisionMove do_collision_movement(CollisionSpawn& s, Vec3f& target, CollisionQueries& q, const Fpu& f) {
    CollisionMove out;
    Frame fr;
    fr.f3c = q.actor_location();
    MovementStat& entry = s.ring[s.ring_index % 20];
    s.ring_index = (s.ring_index + 1) % 20;
    if (s.ring_count < 20) ++s.ring_count;
    for (int k = 0; k < 12; ++k) entry.before[static_cast<std::size_t>(k)] = tf(s.physics[static_cast<std::size_t>(k)]);
    entry.floor_before = tf(s.floor_height);
    for (int k = 0; k < 3; ++k) entry.target[static_cast<std::size_t>(k)] = tf(target[static_cast<std::size_t>(k)]);
    ++s.step_count;
    float* position = s.physics.data();
    if (q.zone_collision_loaded()) {
        Vec3f min{}, max{};
        // No world (-1): the original compares uninitialised locals; taken as inside here.
        if (q.scene_box(min, max) && !inside_scene(f, min, max, target)) {
            position[3] = position[4] = position[5] = 0.0f;
            for (int k = 0; k < 3; ++k) position[k] = tf(s.last_good[static_cast<std::size_t>(k)]);
            out.early = true;
            out.stopped = false; // returns 0
            return out;
        }
    }
    q.zone_client();
    bool collide = false;
    if (!q.mounted() && !q.is_vehicle_race()) collide = !q.has_property(kPropertyNoCollide);

    // Lift (0x50823c..0x5082d2).
    for (int k = 0; k < 3; ++k) fr.f64[static_cast<std::size_t>(k)] = tf(position[k]);
    double h = q.height();
    fr.f70 = to_f(h);                                   // fst F70
    fr.lifted_z = x87_add(f, st(fr.f3c[2]), h);         // fld F44; fadd st, st(1)
    fr.f3c[2] = to_f(fr.lifted_z);                      // fst F44
    target[2] = to_f(x87_add(f, h, st(target[2])));     // fld [tz]; faddp st(2), st: h + tz
    for (int k = 0; k < 3; ++k) position[k] = tf(target[static_cast<std::size_t>(k)]);
    fr.f30 = {tf(fr.f3c[0]), tf(fr.f3c[1]), to_f(fr.lifted_z)};
    fr.f4c = {tf(target[0]), tf(target[1]), tf(target[2])};
    fr.f1c = to_f(x87_sub(f, st(target[0]), st(fr.f3c[0]))); // fsubrp st(2), st: tx - F3c
    fr.f18 = to_f(x87_sub(f, st(target[1]), st(fr.f3c[1])));
    fr.f94 = to_f(x87_sub(f, X87(target[2]), fr.lifted_z));  // fsubr [tz]: tz - X

    float radius = to_f(q.collision_radius());
    // The sweep request (F98): the lifted start, the move and the radius.
    std::array<float, 7> request{tf(fr.f3c[0]), tf(fr.f3c[1]), tf(fr.f3c[2]), tf(fr.f1c), tf(fr.f18), tf(fr.f94),
                                 radius};
    bool keep = false; // F11
    bool moving = q.moving_state();
    float d1 = request[3], d2 = request[4], d3 = request[5]; // F14, F8c, F90
    if (!moving && std::fabs(fr.f1c) < kStill && std::fabs(fr.f18) < kStill && std::fabs(fr.f94) < kStill) {
        if (s.step_count > 2) {
            target = {tf(fr.f3c[0]), tf(fr.f3c[1]), tf(fr.f3c[2])};
            d1 = d2 = d3 = 0.0f;
            if (s.body_wet >= 5 && s.stand_time == 0) s.stand_time = q.display_time();
        }
    } else {
        s.colliding_type = 0;
    }
    if (s.step_count > 2 && d1 < kStill && kStillLow < d1 && d2 < kStill && d2 > kStillLow && kStill > d3 &&
        d3 > kStillLow) {
        if (s.body_wet < 5 && s.stand_time == 0) {
            std::uint32_t time = q.display_time();
            s.stand_time = time;
            if (q.rider()) {
                q.rider();
                q.set_rider_stand_time(time);
            }
        }
        if (!moving && position[6] == 0.0f) {
            keep = true;
            collide = false;
        }
    }
    if (collide) collide = q.sweep_request(request);
    bool rider = q.rider();
    fr.f20 = 0.0f;
    std::int32_t count = q.is_controlled() ? 10 : 1;
    s.possibly_stuck = 0;
    bool hit = false; // F13
    if (collide) {
        fr.f80 = {tf(fr.f30[0]), tf(fr.f30[1]), tf(fr.f30[2])};
        bool slid = false; // F12
        fr.f24 = {tf(target[0]), tf(target[1]), tf(target[2])};
        while (count != 0 || slid) {
            SweepResult r{fr.f4c, fr.f74, fr.f20};
            q.sweep(fr.f80, fr.f24, rider, true, r);
            fr.f4c = r.position;
            fr.f74 = r.skitter_dir;
            fr.f20 = r.skitter_dist;
            --count;
            float step = tf(fr.f20);
            if (!(step > 0.0f) || slid) break;
            hit = true;
            if (count == 0) {
                if (q.is_controlled() && slide_back(f, fr)) {
                    normalize_vector(fr.f58.data(), f);
                    add_back(f, fr);
                    normalize_vector(fr.f74.data(), f);
                    slid = true;
                    if (fr.f20 < kSlideLimit) {
                        step = kSlideLimit;
                        fr.f20 = kSlideLimit;
                    } else {
                        step = tf(fr.f20);
                    }
                } else {
                    step = tf(fr.f20);
                }
            }
            advance(f, fr, step);
        }
    } else {
        q.no_collide();
        if (q.is_vehicle_race()) q.move_without_collision(target, fr.f4c);
    }
    for (int k = 0; k < 3; ++k) position[k] = tf(fr.f4c[static_cast<std::size_t>(k)]);
    Vec3f pos{position[0], position[1], position[2]};
    if (collide && q.is_controlled() && !q.is_vehicle_race() && !keep && s.possibly_stuck == 0) {
        bool fits = q.in_scene_region(pos) && q.can_i_fit_here(pos, hit ? 0 : 1);
        if (!fits) {
            s.possibly_stuck = 1;
            for (int k = 0; k < 3; ++k) position[k] = tf(fr.f64[static_cast<std::size_t>(k)]);
            keep = true;
        }
    }
    pos = {position[0], position[1], position[2]};
    bool barely = barely_moved(f, fr, pos);
    if (s.possibly_stuck == 0) {
        if (q.is_vehicle_race()) {
            keep = s.flag_19d != 0;
        } else if (barely && s.step_count > 2) {
            keep = true;
        } else {
            s.flag_19d = 0;
            if (kFlat > fr.f6c && kMinusOne > position[5]) position[5] = kMinusOne;
            q.clear_stuck(0);
            if (q.check_stuck() && q.is_controlled()) {
                // 0x509190 -> CCollisionInfoSphere::InitFromActor @0x476ab0 and the base @0x441c90.
                CollisionTestInfo info;
                float r = to_f(q.collision_radius());
                info.from = {tf(position[0]), tf(position[1]), tf(position[2])}; // live: check_stuck may move z
                info.to = info.from;
                info.radius = tf(r);
                info.radius_squared = to_f(x87_mul(f, st(r), st(r)));
                info.radius_inverse = to_f(x87_div(f, 1.0, st(r)));
                info.has_object = true;
                info.flag = 1;
                if (q.collision_test(info)) {
                    s.possibly_stuck = 1;
                    collide = false;
                }
            }
            if (q.is_controlled() && collide && !q.is_vehicle_race()) {
                SweepResult r{fr.f24, fr.f64, fr.f14};
                q.sweep(fr.f30, {position[0], position[1], position[2]}, rider, false, r);
                fr.f24 = r.position;
                fr.f64 = r.skitter_dir;
                fr.f14 = r.skitter_dist;
                if (0.0f < fr.f14) s.possibly_stuck = 1;
            }
        }
    }
    if (s.possibly_stuck == 0 && !keep) {
        fr.f3c[0] = tf(position[0]);
        fr.f3c[1] = tf(position[1]);
        fr.f3c[2] = to_f(x87_sub(f, st(position[2]), X87(fr.f70)));
        q.set_actor_location(fr.f3c);
        position[2] = to_f(x87_sub(f, st(position[2]), X87(fr.f70)));
        if (q.is_player_spawn()) q.after_place();
    } else {
        out.held = s.possibly_stuck == 0;
        position[5] = 0.0f;
        for (int k = 0; k < 3; ++k) position[k] = tf(s.last_good[static_cast<std::size_t>(k)]);
    }
    if (hit && s.possibly_stuck == 0 && q.can_swim_jump_now() && !q.on_ladder()) {
        // 0x508aa3..0x508ae8: F14 = |speed|; F58 = the position (z + F70) - F30.
        fr.f14 = to_f(f.sqrt(length_squared(f, st(position[3]), st(position[4]), st(position[5]))));
        double pz = x87_add(f, st(position[2]), X87(fr.f70)); // fadd [F70]
        fr.f58[0] = to_f(x87_sub(f, st(position[0]), X87(fr.f30[0])));
        fr.f58[1] = to_f(x87_sub(f, st(position[1]), X87(fr.f30[1])));
        fr.f58[2] = to_f(x87_sub(f, pz, X87(fr.f30[2])));
        normalize_vector(fr.f58.data(), f);
        fr.f24 = {tf(position[3]), tf(position[4]), tf(position[5])};
        normalize_vector(fr.f24.data(), f);
        // 0x508b0f..0x508b5a: max(F24 . F58, 0) * F14 along F58.
        double p0 = x87_mul(f, st(fr.f24[0]), st(fr.f58[0])); // fmul st(1), st
        double p1 = x87_mul(f, st(fr.f24[1]), st(fr.f58[1]));
        double p01 = x87_add(f, p1, p0);                       // faddp st(1), st
        double p2 = x87_mul(f, st(fr.f24[2]), st(fr.f58[2]));
        double dot = x87_add(f, p2, p01);
        // fldz; fcom st(1); test ah, 0x41: keeps the dot unless 0 > dot.
        double v = 0.0 > dot ? 0.0 : dot;
        v = x87_mul(f, v, X87(fr.f14));                         // fmul [F14]
        position[3] = to_f(x87_mul(f, st(fr.f58[0]), v));       // fmul st(2), st
        position[4] = to_f(x87_mul(f, st(fr.f58[1]), v));       // fmul st(3), st
        position[5] = to_f(x87_mul(f, st(fr.f58[2]), v));       // fmulp st(1), st
    }
    q.finish_move();
    q.after_move(fr.f30, fr.f3c);
    for (int k = 0; k < 12; ++k) entry.after[static_cast<std::size_t>(k)] = tf(s.physics[static_cast<std::size_t>(k)]);
    entry.floor_after = tf(s.floor_height);
    // 0x508bc2..0x508bed: stopped unless the speed's square is above 0.
    double speed2 = length_squared(f, st(position[3]), st(position[4]), st(position[5]));
    out.stopped = !(speed2 > 0.0);
    return out;
}

} // namespace hailstone::game::mechanics
