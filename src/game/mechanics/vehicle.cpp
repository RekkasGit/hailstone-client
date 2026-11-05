#include "game/mechanics/vehicle.h"

#include <cstring>

#include "game/mechanics/controls.h"
#include "game/sine_table.h"

// Each helper follows the original's instruction stream (see the rewrite's
// asm). A value loaded onto the x87 stack (fld) is quiet: S(); a float used
// as a memory operand (fmul/fadd dword) keeps its signalling flag for the
// x87's NaN choice: M(). Intermediates are doubles rounded at the record's
// precision; only the stores (fstp dword) round to float.

namespace nmsc::game::mechanics {

namespace {

float quiet(float v) {
    std::uint32_t b;
    std::memcpy(&b, &v, 4);
    if ((b & 0x7f800000u) == 0x7f800000u && (b & 0x007fffffu)) b |= 0x00400000u;
    std::memcpy(&v, &b, 4);
    return v;
}
X87 S(float v) { return X87(static_cast<double>(quiet(v))); }
X87 S(double v) { return X87(v); }
X87 M(float v) { return X87(v); }
float to_f(double v) { return static_cast<float>(v); }

} // namespace

VehicleFrame vehicle_frame(float heading, float pitch) {
    float sh = cmath_sin_interp(heading), ch = cmath_cos_interp(heading);
    float sp = cmath_sin_interp(pitch), cp = cmath_cos_interp(pitch);
    return {cp * ch, cp * sh, sp, -sh, ch, 0.0f, -sp * ch, -sp * sh, cp};
}

void vehicle_world_to_local(const VehiclePose& v, VehicleBody& b, const VehicleFrame& m, const Fpu& f) {
    // 0x8cfde8: p = M (p - origin), the differences unrounded.
    double dx = x87_sub(f, S(b.x), M(v.x));
    double dy = x87_sub(f, S(b.y), M(v.y));
    double dz = x87_sub(f, S(b.z), M(v.z));
    double r0 = x87_add(f, x87_mul(f, S(m[1]), S(dy)), x87_mul(f, S(m[0]), S(dx)));
    r0 = x87_add(f, r0, x87_mul(f, S(m[2]), S(dz)));
    double r1 = x87_add(f, x87_mul(f, S(m[4]), S(dy)), x87_mul(f, S(m[3]), S(dx)));
    r1 = x87_add(f, r1, x87_mul(f, S(dz), M(m[5])));
    double r2 = x87_add(f, x87_mul(f, S(dx), S(m[6])), x87_mul(f, S(dy), S(m[7])));
    r2 = x87_add(f, r2, x87_mul(f, S(dz), M(m[8])));
    b.x = to_f(r0);
    b.y = to_f(r1);
    b.z = to_f(r2);
    // fld angle; fsub vehicle's; fstp.
    b.heading = to_f(x87_sub(f, S(b.heading), M(v.heading)));
    b.pitch = to_f(x87_sub(f, S(b.pitch), M(v.pitch)));
    // 0x8cfe51: v = M v.
    float vx = b.speed_x, vy = b.speed_y, vz = b.speed_z;
    double s0 = x87_add(f, x87_mul(f, S(m[1]), S(vy)), x87_mul(f, S(m[0]), S(vx)));
    s0 = x87_add(f, s0, x87_mul(f, S(m[2]), S(vz)));
    double s1 = x87_add(f, x87_mul(f, S(m[4]), S(vy)), x87_mul(f, S(m[3]), S(vx)));
    s1 = x87_add(f, s1, x87_mul(f, S(vz), M(m[5])));
    double s2 = x87_add(f, x87_mul(f, S(vx), S(m[6])), x87_mul(f, S(vy), S(m[7])));
    s2 = x87_add(f, s2, x87_mul(f, S(vz), M(m[8])));
    b.speed_x = to_f(s0);
    b.speed_y = to_f(s1);
    b.speed_z = to_f(s2);
}

void vehicle_local_to_world(const VehiclePose& v, VehicleBody& b, const VehicleFrame& m, const Fpu& f) {
    // 0x8cfea6: p = M^T p + origin, all three before any store.
    double a = x87_add(f, x87_mul(f, S(b.x), M(m[1])), x87_mul(f, S(b.y), M(m[4])));
    a = x87_add(f, a, x87_mul(f, S(b.z), M(m[7])));
    double c2 = x87_add(f, x87_mul(f, S(b.x), M(m[2])), x87_mul(f, S(m[5]), M(b.y)));
    c2 = x87_add(f, c2, x87_mul(f, S(m[8]), M(b.z)));
    double c0 = x87_add(f, x87_mul(f, S(b.x), M(m[0])), x87_mul(f, S(m[3]), M(b.y)));
    c0 = x87_add(f, c0, x87_mul(f, S(m[6]), M(b.z)));
    c0 = x87_add(f, c0, M(v.x));
    a = x87_add(f, a, S(v.y));
    c2 = x87_add(f, c2, M(v.z));
    b.x = to_f(c0);
    b.y = to_f(a);
    b.z = to_f(c2);
    // fld vehicle's; fadd angle; fstp; then __FixHeading of the stored float.
    float h = to_f(x87_add(f, S(v.heading), M(b.heading)));
    b.heading = to_f(fix_heading(static_cast<double>(quiet(h)), f));
    float p = to_f(x87_add(f, S(v.pitch), M(b.pitch)));
    b.pitch = to_f(fix_heading(static_cast<double>(quiet(p)), f));
    // 0x8cff32: v = M^T v.
    float vx = b.speed_x, vy = b.speed_y, vz = b.speed_z;
    double t1 = x87_add(f, x87_mul(f, S(vx), M(m[1])), x87_mul(f, S(vy), M(m[4])));
    t1 = x87_add(f, t1, x87_mul(f, S(vz), M(m[7])));
    double t2 = x87_add(f, x87_mul(f, S(vx), M(m[2])), x87_mul(f, S(vy), M(m[5])));
    t2 = x87_add(f, t2, x87_mul(f, S(m[8]), M(vz)));
    double t0 = x87_add(f, x87_mul(f, S(m[3]), M(vy)), x87_mul(f, S(vx), M(m[0])));
    t0 = x87_add(f, t0, x87_mul(f, S(m[6]), M(vz)));
    b.speed_x = to_f(t0);
    b.speed_y = to_f(t1);
    b.speed_z = to_f(t2);
}

} // namespace nmsc::game::mechanics
