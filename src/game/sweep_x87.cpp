#include "game/sweep_x87.h"

namespace nmsc::game {

void SweepX87::load(const Info& in) {
    put3(kInfo + 0x04, in.start);
    put3(kInfo + 0x10, in.end);
    put3(kInfo + 0x1c, in.delta);
    put3(kInfo + 0x28, in.direction);
    put_f(kInfo + 0x34, in.inv_length);
    put_f(kInfo + 0x38, in.t);
    put3(kInfo + 0x3c, in.normal);
    put3(kInfo + 0x48, in.slide);
    put_f(kInfo + 0x54, in.slide_distance);
    wr8(kInfo + 0x58, in.hit ? 1 : 0);
    put_f(kInfo + 0x74, in.radius);
    put3(kInfo + 0x78, in.start);
    put_f(kInfo + 0x84, in.radius_squared);
    put_f(kInfo + 0x88, in.radius_inverse);
}

void SweepX87::save(Info& out) const {
    out.t = get_f(kInfo + 0x38);
    out.normal = get3(kInfo + 0x3c);
    out.slide = get3(kInfo + 0x48);
    out.slide_distance = get_f(kInfo + 0x54);
}

SweepX87::Info SweepX87::make_info(const std::array<float, 3>& from, const std::array<float, 3>& to, float radius) {
    // CCollisionInfoBase @0x441c90 (0x441c9e..0x441d43): start and end through the FPU, the delta (to - from,
    // each rounded), the direction normalised by Vec3_Normalize, 1 / |delta| only when 0 < |delta|.
    Info in;
    for (int k = 0; k < 3; ++k) {
        auto i = static_cast<std::size_t>(k);
        in.start[i] = static_cast<float>(ld(st32(from[i])));
        in.end[i] = static_cast<float>(ld(st32(to[i])));
        std::uint32_t tb, fb;
        std::memcpy(&tb, &to[i], 4);
        std::memcpy(&fb, &from[i], 4);
        in.delta[i] = static_cast<float>(sub(ld(tb), mem(fb))); // fld to; fsub from
    }
    reset_fpu();
    put3(kOut, in.delta);
    for (int k = 0; k < 3; ++k) wr32(kOut + 4u * static_cast<std::uint32_t>(k), st32(ld(rd32(kOut + 4u * k))));
    ecx = kOut;
    call(&SweepX87::f_441bb0);
    in.direction = get3(kOut);
    double length = st(0);
    pop();
    if (0.0 < length) {
        in.inv_length = static_cast<float>(div(1.0, length)); // fld1; fdiv st, st(2)
    } else {
        in.inv_length = 0.0f; // fst: the 0 itself
    }
    in.t = 1.0f;
    // InitFromActor @0x476ab0: the radius, its square and inverse.
    in.radius = radius;
    double r = ld(st32(radius));
    in.radius_squared = static_cast<float>(mul(r, r));
    in.radius_inverse = static_cast<float>(div(1.0, r));
    return in;
}

bool SweepX87::triangle(Info& info, const std::array<float, 3>& normal, const std::array<float, 3>& v0,
                        const std::array<float, 3>& v1, const std::array<float, 3>& v2) {
    reset_fpu();
    load(info);
    put3(kTri, normal);
    put3(kVerts, v0);
    put3(kVerts + 0x10, v1);
    put3(kVerts + 0x20, v2);
    wr32(kTri + 0x10, kVerts);
    wr32(kTri + 0x14, kVerts + 0x10);
    wr32(kTri + 0x18, kVerts + 0x20);
    ecx = kInfo;
    push32(kTri);
    call(&SweepX87::f_7a28e0);
    save(info);
    return (eax & 0xff) != 0;
}

std::array<float, 3> SweepX87::clamp_point(const Info& info) {
    reset_fpu();
    load(info);
    ecx = kInfo;
    push32(kOut);
    call(&SweepX87::f_7a2680);
    return get3(kOut);
}

void SweepX87::skitter(Info& info) {
    reset_fpu();
    load(info);
    ecx = kInfo;
    call(&SweepX87::f_7a2720);
    save(info);
}

} // namespace nmsc::game
