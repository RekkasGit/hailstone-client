#pragma once

// The graphics DLL's sine and cosine tables (CMath, eqgame unk_pinstSinCosTable):
// 512 floats per turn, built by CMath::InitSinCosTables @0x100b9680 with the
// quadrant entries written exactly (rof2decomp notes/eqgraphicsdx9.md, The
// sine table). Angles are in 512ths of a turn.

namespace hailstone::game {

// CMath vtable +0x00 (sin) and +0x04 (cos), 0x100b92e0 / 0x100b9340: the
// 512-entry float tables, linearly interpolated. sin floors negative
// arguments (trunc - 1); cos truncates (so its fraction can be negative).
float cmath_sin_interp(float x);
float cmath_cos_interp(float x);

// CMath vtable +0x08 / +0x0c: table[(int)trunc(x) & 511], no interpolation
// (notes/eqgraphicsdx9.md, The sine table).
float cmath_sin(float x);
float cmath_cos(float x);

} // namespace hailstone::game
