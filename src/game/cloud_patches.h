#pragma once

// The procedural sky's cloud patches (CCloudPatch), as the graphics DLL moves
// them (rof2decomp notes/eqgraphicsdx9.md, Sky):
//   - time base: dt_ms = GetTickCount() delta x TimeScale (1.0), 1 on the
//     first frame and when the delta is 0 (CSkyBase_UpdateFrameTime @0x100c2130);
//   - per frame (CCloudPatch_Update @0x10030020): angle += Velocity dt (not
//     wrapped), rotation = fmod(rotation + Spin dt, 6.2831854); the patch is
//     turned by -angle about (0, 1, 0);
//   - spawning (CCloudDefinition_SpawnPatch @0x10027eb0): every value is
//     rand() / 32767 x (max - min) + min, drawn separately; a fill places
//     patch i of n at i (2 VisibleAngle / n) + (pi / 2 - VisibleAngle);
//   - fades (0x100295f0): in 5000 ms or 0, hold Duration x 1000 ms (0 ->
//     never times out), out 5000 ms.

#include <cstdint>
#include <functional>
#include <vector>

#include "assets/sky_weather.h"

namespace hailstone::game {

struct CloudPatch {
    float angle = 0.0f;    // radians about +Y (+0x38)
    float rotation = 0.0f; // radians, in [0, 2 pi) (+0x60)
    float velocity = 0.0f; // radians per ms
    float spin = 0.0f;     // radians per ms
    float offset = 0.0f;
    float size_x = 0.0f, size_y = 0.0f;
    float fade_in_ms = 0.0f; // 0 for a fill, 5000 for a later spawn
    float hold_ms = 0.0f;    // 0: never times out
    float age_ms = 0.0f;
};

// rand() / 32767 x (max - min) + min with the caller's rand() (0..32767).
float cloud_random(int rand_value, float min, float max);

class CloudLayer {
public:
    // `rand` returns 0..32767 like the C library's rand().
    CloudLayer(assets::CloudDefinition def, std::function<int()> rand);

    // Fills the layer to DesiredCount, spread over the visible arc.
    void fill();
    // Advances by dt_ms of sky time (TimeScale 1.0).
    void update(float dt_ms);

    const assets::CloudDefinition& definition() const { return def_; }
    const std::vector<CloudPatch>& patches() const { return patches_; }
    // Opacity of a patch from its fade timing (in 0 for a fill, hold, out 5000 ms).
    static float opacity(const CloudPatch& p);

private:
    CloudPatch spawn(float angle);

    assets::CloudDefinition def_;
    std::function<int()> rand_;
    std::vector<CloudPatch> patches_;
    float spawn_timer_ms_ = 0.0f;
};

// One frame's patch motion (CCloudPatch_Update).
void advance_cloud_patch(CloudPatch& p, float dt_ms);

} // namespace hailstone::game
