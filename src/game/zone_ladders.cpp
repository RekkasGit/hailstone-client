#include "game/zone_ladders.h"

#include <cctype>
#include <cmath>

#include "assets/zone.h"
#include "game/coords.h"

namespace hailstone::game {

bool is_ladder_actor(std::string_view tag) {
    static constexpr char kTag[] = "LADDER";
    if (tag.size() < 6) return false;
    for (std::size_t i = 0; i < 6; ++i)
        if (std::toupper(static_cast<unsigned char>(tag[i])) != kTag[i]) return false;
    return true;
}

namespace {

// ObjectShape::from_mesh's see-through rule: translucent and additive surfaces.
bool see_through(const assets::Mesh& mesh, const assets::MeshPart& part) {
    if (part.material >= mesh.materials.size()) return false;
    const assets::BlendMode b = mesh.materials[part.material].blend;
    return b != assets::BlendMode::Opaque && b != assets::BlendMode::Masked && b != assets::BlendMode::Invisible;
}

// The ladder's shape: the solid parts as ObjectShape::from_mesh has them, plus the passable parts' triangles marked
// passable. The pick's first pass sets passableBlocking (CCollision::CollideLineSegment's mode 0 with +0x61 = 1), so
// a ladder whose polygons are all passable, as Cabilis's LADDERCB101, is clicked like any other; the movement sweep
// still goes through it. Bounds take both, for the height. False when there is nothing to bound.
bool ladder_shape(const assets::Mesh& mesh, ObjectShape& s) {
    s = ObjectShape::from_mesh(mesh);
    bool any = !s.tris.empty();
    for (const auto& part : mesh.parts) {
        if (part.solid) continue;
        const bool clear = see_through(mesh, part);
        for (std::uint32_t i = 0; i + 2 < part.index_count; i += 3) {
            std::uint32_t idx[3] = {mesh.indices[part.first_index + i], mesh.indices[part.first_index + i + 1],
                                    mesh.indices[part.first_index + i + 2]};
            if (idx[0] >= mesh.vertices.size() || idx[1] >= mesh.vertices.size() || idx[2] >= mesh.vertices.size())
                continue;
            std::uint32_t b = mesh.front_ccw ? idx[2] : idx[1], c = mesh.front_ccw ? idx[1] : idx[2];
            const std::uint8_t flags = clear ? kTrianglePassable | kTriangleTransparent : kTrianglePassable;
            s.records.push_back(CollisionTriangle::make(mesh.vertices[idx[0]].position, mesh.vertices[b].position,
                                                        mesh.vertices[c].position, flags));
            if (clear) continue;
            for (auto k : idx) {
                const glm::vec3 p = mesh.vertices[k].position;
                s.lo = any ? glm::min(s.lo, p) : p;
                s.hi = any ? glm::max(s.hi, p) : p;
                any = true;
            }
        }
    }
    return any;
}

} // namespace

std::vector<ZoneLadder> zone_ladders(const assets::Zone& zone) {
    std::vector<ZoneLadder> out;
    for (const auto& p : zone.placements) {
        if (!is_ladder_actor(p.actor)) continue;
        auto it = zone.objects.find(p.actor);
        if (it == zone.objects.end()) continue;
        ZoneLadder l;
        if (!ladder_shape(it->second, l.shape)) continue;
        l.transform = p.transform;
        const glm::vec3 axis(p.transform[0]);
        const float scale = glm::length(axis);
        if (!(scale > 0.0f) || !std::isfinite(scale)) continue;
        l.center = glm::vec3(p.transform[3]);
        l.bound = l.shape.radius * scale;
        l.ladder.type = mechanics::kLadderSwitchType;
        l.ladder.x = l.center.x;
        l.ladder.y = l.center.y;
        l.ladder.z = l.center.z;
        l.ladder.heading = yaw_to_heading(std::atan2(axis.y, axis.x));
        l.ladder.height = l.shape.hi.z * scale;
        out.push_back(std::move(l));
    }
    return out;
}

} // namespace hailstone::game
