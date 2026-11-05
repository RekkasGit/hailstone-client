#pragma once

// The player's light level P: CActor::GetScaledAmbient (EQGraphicsDX9.dll
// 0x1003a4c0, actor vtable +0xf0), which CDisplay_UpdateSkyWeatherAndFog
// smooths by 0.01 per frame (game/fog.h).
//
// From the DLL's code (read for this port; no vectors):
//  - at most once per 1000 ms (timeGetTime - actor +0x1c > 1000) and only
//    while the actor's flag +0xa is set (cleared after the probe), it sweeps
//    a 0.5-radius sphere down from the actor's position, trying in order:
//      0  from z, 20 down          1  from z + 20, 20 down
//      2  from z, 400 down         3  from z + 100, 100 down
//      4..7  from (x + 10, y), (x - 10, y), (x, y + 10), (x, y - 10) at z + 100, 500 down
//    The first probe whose hit is a zone (region) triangle sets the target
//    +0x10 to that triangle's light (CollisionMesh::triangle_light), at least
//    0.1; a probe without a hit, or hitting a placed object, falls through
//    to the next; with none the target is 0.1;
//  - every call the level +0xc moves toward the target: t = (now - +0x20) x
//    0.004, level += (target - level) t, or level = target once t >= 1.
//
// Not ported exactly: the actor's vtable +0x2a4 offset added to the position
// (taken as 0) and the DLL's own sweep (CCollision's query with its flags;
// the client's movement sweep stands in), which flag +0xa's writers are (here
// the probe runs whenever the actor moved, and first on creation), the
// starting level (here 1), and the EQG terrain path (0x100a3e30), which reads
// the terrain's light at the hit point instead (here the hit triangle's
// vertex alpha, as for WLD zones). docs/unknowns.md, "Light level".

#include <cstdint>

#include <glm/glm.hpp>

namespace nmsc::game {

class CollisionMesh;

class ScaledAmbient {
public:
    // The light level at `now_ms` (timeGetTime) for an actor at `position`.
    float update(std::uint32_t now_ms, const glm::vec3& position, const CollisionMesh* mesh);
    // The probe alone: the target light for an actor at `position`.
    static float probe(const glm::vec3& position, const CollisionMesh& mesh);
    void reset() { *this = ScaledAmbient{}; }
    float target() const { return target_; }
    // The level the last update() returned (the actor's +0xc, what +0xf0 hands back).
    float level() const { return level_; }

private:
    float level_ = 1.0f;    // +0xc
    float target_ = 1.0f;   // +0x10
    std::uint32_t last_probe_ = 0;  // +0x1c
    std::uint32_t last_update_ = 0; // +0x20
    bool dirty_ = true;     // +0xa
    glm::vec3 last_position_{0.0f};
};

} // namespace nmsc::game
