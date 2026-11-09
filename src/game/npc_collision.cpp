#include "game/npc_collision.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string_view>

#include "game/mechanics/collision_movement.h"
#include "game/picking.h"
#include "game/race_models.h"
#include "game/world_state.h"

namespace hailstone::game {

bool is_swarm_pet(const Spawn& s) {
    // EQEmu's NPC::PetOnSpawn: a swarm pet of a player is sent with targetable_with_hotkey off and, by default
    // (Pets:ClientPetsUseOwnerNameInLastName), the last name "<owner>'s Pet"; its petOwnerId stays 0.
    static constexpr std::string_view kPetSuffix = "'s Pet";
    if (!s.is_npc) return false;
    if (!s.cyclable) return true;
    const std::string_view last = s.last_name;
    return last.size() > kPetSuffix.size() && last.ends_with(kPetSuffix);
}

bool npc_blocks_player(const Spawn& s, std::uint32_t controlled_id) {
    if (!s.is_npc || s.is_corpse || s.id == controlled_id) return false;
    if (s.pet_owner_id != 0 || s.mercenary) return false;
    if (is_swarm_pet(s)) return false;
    if (s.rider_id != 0) return false;                 // someone's mount
    if (is_vehicle_race(s.race, s.gender)) return false; // a boat's hull is its own collision (game/boats.h)
    return true;
}

NpcBody npc_body(const Spawn& s, const glm::vec3& position) {
    NpcBody b;
    b.id = s.id;
    const float ah = s.heights.avatar_height;
    b.feet = position - glm::vec3(0.0f, 0.0f, ah);
    b.radius = static_cast<float>(mechanics::collision_volume_radius(ah, true, s.race, false));
    b.height = std::max(2.0f * ah, kNpcMinWallHeight);
    return b;
}

namespace {

using Walls = std::array<CollisionTriangle, 2 * kNpcWallSides>;

// The walls into `out`; how many (none without a radius).
std::size_t fill_walls(const NpcBody& b, Walls& out) {
    if (!(b.radius > 0.0f)) return 0;
    const float bottom = b.feet.z - kNpcWallBelowFeet;
    const float top = b.feet.z + b.height;
    auto at = [&](int i, float z) {
        const double a = 2.0 * std::numbers::pi * i / kNpcWallSides;
        return glm::vec3(b.feet.x + b.radius * static_cast<float>(std::cos(a)),
                         b.feet.y + b.radius * static_cast<float>(std::sin(a)), z);
    };
    // The corners go round counter-clockwise seen from above; CollisionTriangle's normal,
    // (v0 - v1) x (v2 - v1), then points away from the axis.
    std::size_t n = 0;
    for (int i = 0; i < kNpcWallSides; ++i) {
        const glm::vec3 b0 = at(i, bottom), b1 = at(i + 1, bottom), t0 = at(i, top), t1 = at(i + 1, top);
        out[n++] = CollisionTriangle::make(b1, b0, t0, 0);
        out[n++] = CollisionTriangle::make(b1, t0, t1, 0);
    }
    return n;
}

} // namespace

std::vector<CollisionTriangle> npc_walls(const NpcBody& b) {
    Walls walls;
    const std::size_t n = fill_walls(b, walls);
    return {walls.begin(), walls.begin() + static_cast<std::ptrdiff_t>(n)};
}

namespace {

bool starts_inside(const SphereSweep& s, const NpcBody& b) {
    const glm::vec2 d(s.start.x - b.feet.x, s.start.y - b.feet.y);
    const float reach = b.radius + s.radius - kNpcInsideSlack;
    if (reach <= 0.0f || glm::dot(d, d) >= reach * reach) return false;
    return s.start.z > b.feet.z - kNpcWallBelowFeet - s.radius && s.start.z < b.feet.z + b.height + s.radius;
}

} // namespace

std::uint32_t sweep_npcs(SphereSweep& s, std::span<const NpcBody> bodies) {
    std::uint32_t stopped = 0;
    for (const NpcBody& b : bodies) {
        if (!(b.radius > 0.0f)) continue;
        const float half = (b.height + kNpcWallBelowFeet) * 0.5f;
        const glm::vec3 centre(b.feet.x, b.feet.y, b.feet.z - kNpcWallBelowFeet + half);
        const float reach = std::sqrt(b.radius * b.radius + half * half) + s.radius;
        if (point_segment_distance_squared(PickSegment{s.start, s.delta}, centre) > reach * reach) continue;
        if (starts_inside(s, b)) continue;
        bool hit = false;
        // On the stack: the sweep asks this of every NPC near the mover, every frame.
        Walls walls;
        const std::size_t n = fill_walls(b, walls);
        for (std::size_t i = 0; i < n; ++i) hit = collide_triangle(s, walls[i]) || hit;
        if (hit) stopped = b.id;
    }
    return stopped;
}

} // namespace hailstone::game
