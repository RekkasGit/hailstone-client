#include "game/sine_table.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace nmsc::game {

namespace {

// CMath::InitSinCosTables @0x100b9680: cos[i] = (float)cos(i pi / 256),
// sin[(i + 128) mod 512] = cos[i], quadrant entries written exactly.
struct Tables {
    std::array<float, 512> sin{}, cos{};
    Tables() {
        for (int i = 0; i < 512; ++i) cos[i] = static_cast<float>(std::cos(i * std::numbers::pi / 256.0));
        for (int i = 0; i < 512; ++i) sin[(i + 128) % 512] = cos[i];
        sin[0] = 0.0f;
        sin[128] = 1.0f;
        sin[256] = 0.0f;
        sin[384] = -1.0f;
        cos[0] = 1.0f;
        cos[128] = 0.0f;
        cos[256] = -1.0f;
        cos[384] = 0.0f;
    }
};

const Tables& tables() {
    static const Tables t;
    return t;
}

float interp(const std::array<float, 512>& t, int i, float frac) {
    int idx = i & 511;
    float a = t[static_cast<std::size_t>(idx)];
    float b = idx < 511 ? t[static_cast<std::size_t>(idx) + 1] : t[0];
    return a + (b - a) * frac;
}

} // namespace

float cmath_sin_interp(float x) {
    auto i = static_cast<int>(std::trunc(x));
    if (i < 0) --i;
    return interp(tables().sin, i, x - static_cast<float>(i));
}

float cmath_cos_interp(float x) {
    auto i = static_cast<int>(std::trunc(x));
    return interp(tables().cos, i, x - static_cast<float>(i));
}

namespace {
// (int)x & 511. A NaN, an infinity or a value past int32 is not cast (undefined in C++): the original's cvttss2si
// gives INT_MIN for those, which is entry 0.
std::size_t table_index(float x) {
    if (!(std::fabs(x) < 2147483648.0f)) return 0;
    return static_cast<std::size_t>(static_cast<int>(std::trunc(x)) & 511);
}
} // namespace

float cmath_sin(float x) { return tables().sin[table_index(x)]; }

float cmath_cos(float x) { return tables().cos[table_index(x)]; }

} // namespace nmsc::game
