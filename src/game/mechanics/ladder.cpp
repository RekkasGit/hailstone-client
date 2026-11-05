#include "game/mechanics/ladder.h"

#include <cmath>
#include <cstring>

#include "game/mechanics/controls.h"

// Follows the rewrite's asm helpers instruction by instruction. A value on the
// x87 stack is a quiet double; a float memory operand is X87(float).

namespace nmsc::game::mechanics {

namespace {

constexpr float kHalfTurn = 256.0f;  // 0x9c7328
constexpr float kFullTurn = 512.0f;  // 0x9c7c68
constexpr float kTwo = 2.0f;         // 0x9c58e8
constexpr float kHalf = 0.5f;        // 0x9c4998
constexpr float kReach = 25.0f;      // 0x9c7358
constexpr float kTopShare = 0.7f;    // 0x9cf8b8
constexpr float kMinHeight = 1.5f;   // 0x9c5a14
constexpr float kLookDown = -75.0f;  // 0x9cf8b4
constexpr float kOne = 1.0f;         // 0x9c3390
constexpr float kNoFloor = -1e27f;   // 0x9c5760

float quiet(float v) { // fld / fstp dword
    std::uint32_t b;
    std::memcpy(&b, &v, 4);
    if ((b & 0x7f800000u) == 0x7f800000u && (b & 0x007fffffu)) b |= 0x00400000u;
    std::memcpy(&v, &b, 4);
    return v;
}
X87 S(float v) { return X87(static_cast<double>(quiet(v))); }
X87 S(double v) { return X87(v); }
float to_f(double v) { return static_cast<float>(v); }

struct Cmp {
    bool c0, c3, unordered;
};
Cmp cmp(double a, double b) {
    if (std::isnan(a) || std::isnan(b)) return {true, true, true};
    return {a < b, a == b, false};
}

// fld *a / fadd *b / fstp.
float sum(const Fpu& f, float a, float b) { return to_f(x87_add(f, S(a), X87(b))); }
// __FixHeading of a float argument, stored as a float.
float fix(const Fpu& f, float h) { return to_f(fix_heading(static_cast<double>(quiet(h)), f)); }

} // namespace

double ladder_heading_diff(float a, float b, const Fpu& f) {
    double x = S(b).v, y = S(a).v;            // fld b; fld a
    Cmp c = cmp(y, x);                        // fcom st(1); test ah, 0x41
    double d = (!c.c0 && !c.c3) ? x87_sub(f, y, x) : x87_sub(f, x, y);
    Cmp h = cmp(static_cast<double>(kHalfTurn), d); // fld 256; fcomp st(1); test ah, 5; jp
    if (!(h.c0 && !h.c3)) return d;
    return -x87_sub(f, d, X87(kFullTurn));    // fsub 512; fchs
}

bool mount_clicked_ladder(const LadderSwitch& ladder, LadderPlayer& p, LadderQueries& q, const Fpu& f) {
    if (ladder.type != kLadderSwitchType) return false;
    if (p.movement_mode == kLadderMovementMode) {
        double diff = ladder_heading_diff(quiet(ladder.heading), quiet(p.heading), f);
        Cmp c = cmp(static_cast<double>(kHalfTurn), diff); // fld 256; fcom st(1): 256 > diff (ordered)
        if (!c.c0 && !c.c3) p.heading = quiet(ladder.heading);
        else p.heading = fix(f, sum(f, kHalfTurn, ladder.heading));
        return true;
    }
    // StepBack: max(h, 2) (h when 2 is below it, equal or unordered), + 0.5.
    double actor = q.actor_height();
    Cmp two = cmp(static_cast<double>(kTwo), actor);
    double a = (two.c0 || two.c3) ? actor : static_cast<double>(kTwo);
    float step_back = to_f(x87_add(f, S(a), X87(kHalf)));
    double lh = q.ladder_height();
    float height = to_f(lh);                                  // fst
    float top = to_f(x87_add(f, S(lh), X87(ladder.z)));       // fadd z; fstp
    // CDisplay::TrueDistance(player, (ladder x, y, top), 0): the 2-D root of dy^2 + dx^2.
    double dx = x87_sub(f, S(p.x), S(ladder.x));
    double dy = x87_sub(f, S(p.y), S(ladder.y));
    float squared = to_f(x87_add(f, x87_mul(f, S(dy), S(dy)), x87_mul(f, S(dx), S(dx))));
    double distance = q.root(squared);
    Cmp reach = cmp(distance, static_cast<double>(kReach)); // fcomp 25: less, equal or unordered go on
    if (!(reach.c0 || reach.c3)) {
        q.too_far();
        return false;
    }
    double share = x87_add(f, x87_mul(f, S(height), X87(kTopShare)), X87(ladder.z));
    if (!cmp(share, static_cast<double>(p.z)).c0) return false; // fcomp z: C0 (below the feet, or NaN)
    // StandZ: top - AvatarHeight when 1.5 is below AvatarHeight, else 1.5.
    Cmp m = cmp(static_cast<double>(kMinHeight), static_cast<double>(p.avatar_height));
    bool keep = m.unordered || !m.c0;
    float stand_z = keep ? kMinHeight : to_f(x87_sub(f, S(top), X87(p.avatar_height)));
    auto along = [&](double trig, float base) { return to_f(x87_add(f, x87_mul(f, S(trig), X87(step_back)), X87(base))); };
    float heading = fix(f, sum(f, ladder.heading, kHalfTurn));
    float sx = along(q.cosine(heading), ladder.x);
    float sy = along(q.sine(heading), ladder.y);
    if (q.can_i_fit_here(sx, sy, stand_z)) {
        p.x = quiet(sx);
        p.y = quiet(sy);
        p.z = quiet(stand_z);
        p.heading = quiet(ladder.heading);
    } else {
        heading = quiet(ladder.heading);
        sx = along(q.cosine(heading), ladder.x);
        sy = along(q.sine(heading), ladder.y);
        if (!q.can_i_fit_here(sx, sy, stand_z)) return false;
        p.x = quiet(sx);
        p.y = quiet(sy);
        p.z = quiet(stand_z);
        p.heading = fix(f, sum(f, ladder.heading, kHalfTurn));
    }
    q.update_player_actor();
    float push = sum(f, step_back, kTwo);
    p.speed_x = to_f(x87_mul(f, S(q.cosine(quiet(p.heading))), X87(push)));
    double s = q.sine(quiet(p.heading));
    p.movement_mode = kLadderMovementMode;
    p.speed_y = to_f(x87_mul(f, S(s), X87(push)));
    p.speed_z = 0.0f;
    p.speed_run = 0.0f;
    p.pitch = kLookDown;
    p.standing_on_vehicle = false; // 0x8cfd10(player, null)
    p.field_2d4 = 0;
    double floor = q.floor_height(quiet(p.x), quiet(p.y), sum(f, p.z, kOne), 1);
    // fucompp with -1e27: only exactly equal means none (NaN counts as a floor).
    if (!(floor == static_cast<double>(kNoFloor))) p.floor_height = to_f(floor);
    return true;
}

} // namespace nmsc::game::mechanics
