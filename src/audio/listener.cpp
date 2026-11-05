#include "audio/listener.h"

#include <bit>
#include <cctype>
#include <cmath>
#include <numbers>

namespace nmsc::audio {

namespace {

using game::mechanics::Fpu;
using game::mechanics::FpuPrecision;
using game::mechanics::X87;

struct ZonePreset {
    const char* zone;
    std::int32_t outside, high, low;
};
constexpr ZonePreset kZonePresets[] = {
#include "audio/environment_presets.inc"
};

constexpr double kTwoPi = 6.28318530717958;     // 0x9ca188 (the exe's double, not std::numbers::pi * 2)
constexpr double kOneOver512 = 0.001953125;     // 0x9ca180
constexpr std::uint32_t kQuietBit = 0x00400000u;
constexpr std::uint32_t kIndefinite = 0xffc00000u; // the x87's default NaN stored as a float

float as_float(std::uint32_t b) { return std::bit_cast<float>(b); }
std::uint32_t bits_of(float f) { return std::bit_cast<std::uint32_t>(f); }

// fld dword / fstp dword: a signalling NaN comes out quiet; everything else is unchanged.
std::uint32_t through_fpu(std::uint32_t b) { return std::isnan(as_float(b)) ? b | kQuietBit : b; }

bool same_name(std::string_view a, const char* b) {
    std::size_t i = 0;
    for (; i < a.size() && b[i] != 0; ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return i == a.size() && b[i] == 0;
}

// A small unsigned integer of up to 70 bits (hi:lo).
struct Wide {
    std::uint64_t lo = 0, hi = 0;
    bool operator<(const Wide& o) const { return hi != o.hi ? hi < o.hi : lo < o.lo; }
    Wide minus(const Wide& o) const { return {lo - o.lo, hi - o.hi - (lo < o.lo ? 1 : 0)}; }
    Wide twice() const { return {lo << 1, (hi << 1) | (lo >> 63)}; }
    double value() const { return std::ldexp(static_cast<double>(hi), 64) + static_cast<double>(lo); }
};

// The x87's fsin / fcos argument reduction: by pi/2 with its 66-bit pi (0x3243F6A8885A308D3 / 2^64), done exactly
// on the integer significand. Returns the remainder in [-pi/4, pi/4] (approximately) and the quadrant.
void reduce(float operand, double& r, unsigned& quadrant) {
    double a = std::fabs(static_cast<double>(operand));
    quadrant = 0;
    if (a <= std::numbers::pi / 4) {
        r = a;
        return;
    }
    int e = 0;
    double fr = std::frexp(a, &e);                           // a = fr * 2^e, fr in [0.5, 1)
    auto m = static_cast<std::uint64_t>(std::ldexp(fr, 24)); // a = m * 2^(e - 24), m < 2^24
    int shift = e - 24 + 65;                                 // a * 2^65 = m * 2^shift (a >= pi/4: shift >= 40)
    const Wide half_pi{0x243F6A8885A308D3ull, 0x3u};         // (pi66 / 2) * 2^65
    // m * 2^shift mod half_pi, one doubling at a time; the quotient's low two bits follow along.
    Wide rem{m, 0};
    unsigned k = 0;
    for (int i = 0; i < shift; ++i) {
        rem = rem.twice();
        k = (k << 1) & 3u;
        if (!(rem < half_pi)) {
            rem = rem.minus(half_pi);
            k = (k + 1) & 3u;
        }
    }
    double rd;
    Wide half_half{(half_pi.lo >> 1) | (half_pi.hi << 63), half_pi.hi >> 1};
    if (half_half < rem) {
        rd = -half_pi.minus(rem).value();
        k = (k + 1) & 3u;
    } else {
        rd = rem.value();
    }
    r = std::ldexp(rd, -65);
    quadrant = k;
}

// Shared by fsin and fcos: NaN, infinity and the out-of-range operand.
std::optional<std::uint32_t> special(std::uint32_t operand) {
    float f = as_float(operand);
    if (std::isnan(f)) return operand | kQuietBit;
    if (std::isinf(f)) return kIndefinite;
    if (std::fabs(f) >= 9223372036854775808.0f) return operand; // C2 set, st(0) unchanged
    return std::nullopt;
}

} // namespace

std::optional<std::int32_t> listener_environment(bool have_player, float ceiling, float floor, bool indoors,
                                                 FpuPrecision precision) {
    if (!have_player) return std::nullopt;
    Fpu fpu(precision);
    double d = game::mechanics::x87_sub(fpu, X87(ceiling), X87(floor));
    std::int32_t h = Fpu::ftol(std::fabs(d));
    if (h < 0x15) return kEnvironmentLow;
    if (h < 0x1f5 && indoors) return kEnvironmentHigh;
    return kEnvironmentOutside;
}

RoomReverb miles_room_reverb(std::int32_t room) {
    static constexpr RoomReverb kRooms[26] = {
        {0.0f, 1.49300003f, 0.5f, 0.0109999999f},                 // 0 generic
        {0.25f, 0.100000001f, 0.0f, 0.00200000009f},              // 1 padded cell
        {0.416999996f, 0.400000006f, 0.666000009f, 0.00300000003f}, // 2 room
        {0.75f, 1.49899995f, 0.165999994f, 0.0109999999f},        // 3 bathroom
        {0.208000004f, 0.477999985f, 0.0f, 0.00400000019f},       // 4 living room
        {0.300000012f, 2.30900002f, 0.680000007f, 0.0170000009f}, // 5 stone room
        {0.402999997f, 4.27899981f, 0.5f, 0.0299999993f},         // 6 auditorium
        {0.5f, 3.96099997f, 0.5f, 0.0289999992f},                 // 7 concert hall
        {0.5f, 2.88599992f, 1.30400002f, 0.0219999999f},          // 8 cave
        {0.361000001f, 7.28399992f, 0.331999987f, 0.0299999993f}, // 9 arena
        {0.5f, 10.0f, 0.300000012f, 0.0299999993f},               // 10 hangar
        {0.152999997f, 0.259000003f, 0.550000012f, 0.0299999993f}, // 11 carpeted hallway
        {0.361000001f, 1.49300003f, 0.5f, 0.0109999999f},         // 12 hallway
        {0.444000006f, 2.69700003f, 0.638000011f, 0.0199999996f}, // 13 stone corridor
        {0.25f, 1.75199997f, 0.776000023f, 0.0109999999f},        // 14 alley
        {0.111000001f, 3.14499998f, 0.472000003f, 0.0879999995f}, // 15 forest
        {0.111000001f, 2.76699996f, 0.224000007f, 0.0109999999f}, // 16 city
        {0.200000003f, 0.800000012f, 0.200000003f, 0.300000012f}, // 17 mountains
        {0.550000012f, 3.0f, 0.550000012f, 0.0430000015f},        // 18 quarry
        {0.119999997f, 0.800000012f, 0.180000007f, 0.100000001f}, // 19 plain
        {0.208000004f, 1.65199995f, 1.5f, 0.0120000001f},         // 20 parking lot
        {0.65200001f, 2.88599992f, 0.25f, 0.0209999997f},         // 21 sewer pipe
        {1.0f, 1.49899995f, 0.100000001f, 0.0109999999f},         // 22 underwater
        {0.875f, 8.3920002f, 1.38800001f, 0.0109999999f},         // 23 drugged
        {0.138999999f, 10.0f, 1.5f, 0.0109999999f},               // 24 dizzy
        {0.486000001f, 7.5630002f, 4.0f, 0.0109999999f},          // 25 psychotic
    };
    return room >= 0 && room < 26 ? kRooms[room] : kRooms[0];
}

RoomPresets zone_room_presets(std::string_view zone) {
    for (const ZonePreset& p : kZonePresets)
        if (same_name(zone, p.zone)) return {p.outside, p.high, p.low};
    return {};
}

ListenerLocation eq_listener_location(std::uint32_t y, std::uint32_t x, std::uint32_t z, std::uint32_t heading,
                                      FpuPrecision precision) {
    Fpu fpu(precision);
    ListenerLocation out;
    double t = game::mechanics::x87_mul(fpu, X87(as_float(heading)), X87(kTwoPi));
    t = game::mechanics::x87_mul(fpu, X87(t), X87(kOneOver512));
    out.angle = std::isnan(t) ? bits_of(static_cast<float>(t)) | kQuietBit : bits_of(static_cast<float>(t));
    out.z = through_fpu(z) ^ 0x80000000u; // fchs
    out.y = through_fpu(x);
    out.x = through_fpu(y);
    return out;
}

std::uint32_t x87_fsin(std::uint32_t operand) {
    if (auto s = special(operand)) return *s;
    float f = as_float(operand);
    if (f == 0.0f) return operand;
    double r;
    unsigned q;
    reduce(f, r, q);
    double v = q == 0 ? std::sin(r) : q == 1 ? std::cos(r) : q == 2 ? -std::sin(r) : -std::cos(r);
    if (f < 0) v = -v;
    return bits_of(static_cast<float>(v));
}

std::uint32_t x87_fcos(std::uint32_t operand) {
    if (auto s = special(operand)) return *s;
    double r;
    unsigned q;
    reduce(as_float(operand), r, q);
    double v = q == 0 ? std::cos(r) : q == 1 ? -std::sin(r) : q == 2 ? -std::cos(r) : std::sin(r);
    return bits_of(static_cast<float>(v));
}

ListenerOrientation sound_manager_listener(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t angle) {
    ListenerOrientation o;
    o.position[0] = o.stored[0] = through_fpu(x);
    o.position[1] = o.stored[1] = through_fpu(y);
    o.position[2] = o.stored[2] = through_fpu(z);
    o.stored[3] = through_fpu(angle);
    o.face[0] = x87_fcos(angle);
    o.face[1] = x87_fsin(angle);
    o.face[2] = 0;
    o.up[0] = o.up[1] = 0;
    o.up[2] = bits_of(-1.0f);
    return o;
}

bool legal_player_race(std::int32_t race, std::int32_t spawn_race) {
    std::int32_t r = race == -1 ? spawn_race : race;
    return r <= 12 || r == 128 || r == 130 || r == 330 || r == 522;
}

} // namespace nmsc::audio
