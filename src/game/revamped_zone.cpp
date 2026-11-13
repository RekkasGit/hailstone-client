#include "game/revamped_zone.h"

#include "core/log.h"

namespace hailstone::game {

std::optional<float> safe_point_height(const CollisionMesh& mesh, const glm::vec3& safe_world) {
    auto floor = mesh.floor_below(safe_world, kSafePointStepUp);
    if (!floor) return std::nullopt;
    return safe_world.z - *floor;
}

bool safe_point_on_floor(const CollisionMesh& mesh, const glm::vec3& safe_world) {
    auto height = safe_point_height(mesh, safe_world);
    return height && *height <= kSafePointMaxHeight;
}

assets::RevampedZoneTest safe_point_test(const glm::vec3& safe_world, std::optional<CollisionMesh>& built) {
    return [safe_world, &built](const assets::Zone& zone) {
        built = CollisionMesh::build(zone);
        auto height = safe_point_height(*built, safe_world);
        bool on_floor = height && *height <= kSafePointMaxHeight;
        if (height)
            log::info("{}: the server's safe point (world {:.1f} {:.1f} {:.1f}) stands {:.1f} above the live .eqg's "
                      "floor: {}",
                      zone.name, safe_world.x, safe_world.y, safe_world.z, *height,
                      on_floor ? "the server runs the revamped zone" : "not the server's zone");
        else
            log::info("{}: the server's safe point (world {:.1f} {:.1f} {:.1f}) has no floor under it in the live "
                      ".eqg: not the server's zone",
                      zone.name, safe_world.x, safe_world.y, safe_world.z);
        return on_floor;
    };
}

} // namespace hailstone::game
