#include "game/collision.h"

#include <algorithm>
#include <bit>
#include <cmath>

#include "assets/zone.h"
#include "game/mechanics/fpu.h"
#include "game/zone_ladders.h"

namespace nmsc::game {

namespace {

// The material side of a triangle record's bit 1 ("no material, or a
// transparent one"): here every surface that is not drawn opaque or masked
// and is not an invisible collision surface.
bool transparent(assets::BlendMode b) {
    using assets::BlendMode;
    return !(b == BlendMode::Opaque || b == BlendMode::Masked || b == BlendMode::Invisible);
}

bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// 0x1001fa60: fld t[a] / fadd t[b] / fadd t[c] / fmul 0.33333299 (0x101338cc),
// t[i] = i / 256 (CRender +0x85a0), at the game's 24-bit precision; the
// caller stores it to a float.
float vertex_alpha_light(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    const mechanics::Fpu fpu;
    auto t = [](std::uint32_t colour) { return static_cast<double>(static_cast<float>(colour >> 24) / 256.0f); };
    double sum = fpu.add(fpu.add(t(a), t(b)), t(c));
    return static_cast<float>(fpu.mul(sum, static_cast<double>(std::bit_cast<float>(0x3eaaaa9fu))));
}

// The height of the triangle's plane at q when q lies inside it in XY (not for vertical or degenerate ones).
std::optional<float> height_in(const CollisionTriangle& tri, const glm::vec2& q) {
    const glm::vec2 a(tri.v0), b(tri.v1), c(tri.v2);
    const float d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (std::abs(d) < 1e-6f) return std::nullopt;
    const float u = ((b.y - c.y) * (q.x - c.x) + (c.x - b.x) * (q.y - c.y)) / d;
    const float v = ((c.y - a.y) * (q.x - c.x) + (a.x - c.x) * (q.y - c.y)) / d;
    const float w = 1.0f - u - v;
    if (u < 0.0f || v < 0.0f || w < 0.0f) return std::nullopt;
    return u * tri.v0.z + v * tri.v1.z + w * tri.v2.z;
}

} // namespace

CollisionMesh CollisionMesh::build(const assets::Zone& zone) {
    CollisionMesh m;
    auto add_mesh = [&](const assets::Mesh& mesh, const glm::mat4& transform, bool terrain, bool ladder) {
        std::vector<glm::vec3> world(mesh.vertices.size());
        for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
            world[i] = glm::vec3(transform * glm::vec4(mesh.vertices[i].position, 1.0f));
        for (const auto& part : mesh.parts) {
            std::uint8_t flags = part.solid ? 0 : kTrianglePassable;
            if (!terrain) flags |= kTriangleObject;
            if (ladder) flags |= kTriangleLadder;
            if (part.material >= mesh.materials.size() || transparent(mesh.materials[part.material].blend))
                flags |= kTriangleTransparent;
            if (part.material < mesh.materials.size() &&
                mesh.materials[part.material].blend == assets::BlendMode::Invisible)
                flags |= kTriangleInvisible;
            for (std::uint32_t i = 0; i + 2 < part.index_count; i += 3) {
                auto ia = mesh.indices[part.first_index + i], ib = mesh.indices[part.first_index + i + 1],
                     ic = mesh.indices[part.first_index + i + 2];
                if (ia >= world.size() || ib >= world.size() || ic >= world.size()) continue;
                if (!finite(world[ia]) || !finite(world[ib]) || !finite(world[ic])) continue;
                if (mesh.front_ccw) std::swap(ib, ic);
                m.triangles_.push_back(CollisionTriangle::make(world[ia], world[ib], world[ic], flags));
                m.light_.push_back(terrain ? vertex_alpha_light(mesh.vertices[ia].color, mesh.vertices[ib].color,
                                                                mesh.vertices[ic].color)
                                           : std::numeric_limits<float>::quiet_NaN());
                if (terrain) {
                    for (auto k : {ia, ib, ic}) {
                        if (!m.bounds_) m.bounds_.emplace(world[k], world[k]);
                        m.bounds_->first = glm::min(m.bounds_->first, world[k]);
                        m.bounds_->second = glm::max(m.bounds_->second, world[k]);
                    }
                }
            }
        }
    };
    for (const auto& mesh : zone.terrain) add_mesh(mesh, glm::mat4(1.0f), true, false);
    for (const auto& p : zone.placements) {
        auto it = zone.objects.find(p.actor);
        if (it != zone.objects.end()) add_mesh(it->second, p.transform, false, is_ladder_actor(p.actor));
    }
    m.index();
    return m;
}

CollisionMesh CollisionMesh::from_triangles(std::vector<CollisionTriangle> triangles) {
    CollisionMesh m;
    m.triangles_ = std::move(triangles);
    for (const auto& t : m.triangles_) {
        for (const glm::vec3& v : {t.v0, t.v1, t.v2}) {
            if (!m.bounds_) m.bounds_.emplace(v, v);
            m.bounds_->first = glm::min(m.bounds_->first, v);
            m.bounds_->second = glm::max(m.bounds_->second, v);
        }
    }
    m.index();
    return m;
}

void CollisionMesh::index() {
    for (std::uint32_t t = 0; t < triangles_.size(); ++t) {
        const auto& tri = triangles_[t];
        glm::vec3 lo = glm::min(glm::min(tri.v0, tri.v1), tri.v2);
        glm::vec3 hi = glm::max(glm::max(tri.v0, tri.v1), tri.v2);
        auto x0 = static_cast<std::int32_t>(std::floor(lo.x / kCell)), x1 = static_cast<std::int32_t>(std::floor(hi.x / kCell));
        auto y0 = static_cast<std::int32_t>(std::floor(lo.y / kCell)), y1 = static_cast<std::int32_t>(std::floor(hi.y / kCell));
        if (static_cast<std::int64_t>(x1 - x0 + 1) * (y1 - y0 + 1) > 4096) {
            big_.push_back(t); // a huge triangle: tested by every sweep
            continue;
        }
        for (auto x = x0; x <= x1; ++x)
            for (auto y = y0; y <= y1; ++y) cells_[cell_key(x, y)].push_back(t);
    }
}

std::optional<float> CollisionMesh::floor_below(const glm::vec3& p, float step_up) const {
    auto it = cells_.find(cell_key(static_cast<std::int32_t>(std::floor(p.x / kCell)),
                                   static_cast<std::int32_t>(std::floor(p.y / kCell))));
    if (it == cells_.end()) return std::nullopt;
    std::optional<float> best;
    const float limit = p.z + step_up;
    for (std::uint32_t t : it->second) {
        const auto& tri = triangles_[t];
        if (!probe_solid(tri)) continue;
        // Barycentric test in XY, then the plane height at (x, y).
        glm::vec2 a(tri.v0), b(tri.v1), c(tri.v2), q(p);
        float d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        if (std::abs(d) < 1e-6f) continue; // vertical or degenerate
        float u = ((b.y - c.y) * (q.x - c.x) + (c.x - b.x) * (q.y - c.y)) / d;
        float v = ((c.y - a.y) * (q.x - c.x) + (a.x - c.x) * (q.y - c.y)) / d;
        float w = 1.0f - u - v;
        if (u < 0.0f || v < 0.0f || w < 0.0f) continue;
        float z = u * tri.v0.z + v * tri.v1.z + w * tri.v2.z;
        if (z <= limit && (!best || z > *best)) best = z;
    }
    return best;
}

std::optional<float> CollisionMesh::ceiling_above(const glm::vec3& p) const {
    auto it = cells_.find(cell_key(static_cast<std::int32_t>(std::floor(p.x / kCell)),
                                   static_cast<std::int32_t>(std::floor(p.y / kCell))));
    if (it == cells_.end()) return std::nullopt;
    std::optional<float> best;
    for (std::uint32_t t : it->second) {
        const auto& tri = triangles_[t];
        if (!probe_solid(tri)) continue;
        glm::vec2 a(tri.v0), b(tri.v1), c(tri.v2), q(p);
        float d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        if (std::abs(d) < 1e-6f) continue;
        float u = ((b.y - c.y) * (q.x - c.x) + (c.x - b.x) * (q.y - c.y)) / d;
        float v = ((c.y - a.y) * (q.x - c.x) + (a.x - c.x) * (q.y - c.y)) / d;
        float w = 1.0f - u - v;
        if (u < 0.0f || v < 0.0f || w < 0.0f) continue;
        float z = u * tri.v0.z + v * tri.v1.z + w * tri.v2.z;
        if (z > p.z && (!best || z < *best)) best = z;
    }
    return best;
}

std::vector<CollisionMesh::SurfaceHit> CollisionMesh::surfaces_above(const glm::vec3& p) const {
    std::vector<SurfaceHit> out;
    auto it = cells_.find(cell_key(static_cast<std::int32_t>(std::floor(p.x / kCell)),
                                   static_cast<std::int32_t>(std::floor(p.y / kCell))));
    if (it == cells_.end()) return out;
    for (std::uint32_t t : it->second) {
        const auto& tri = triangles_[t];
        if (!probe_solid(tri)) continue;
        if (const auto z = height_in(tri, glm::vec2(p)); z && *z > p.z) out.push_back({*z, tri.normal});
    }
    std::sort(out.begin(), out.end(), [](const SurfaceHit& a, const SurfaceHit& b) { return a.z < b.z; });
    return out;
}

std::optional<float> CollisionMesh::ground_below(const glm::vec3& p) const {
    auto it = cells_.find(cell_key(static_cast<std::int32_t>(std::floor(p.x / kCell)),
                                   static_cast<std::int32_t>(std::floor(p.y / kCell))));
    if (it == cells_.end()) return std::nullopt;
    std::optional<float> best;
    for (std::uint32_t t : it->second) {
        const auto& tri = triangles_[t];
        if (!probe_solid(tri) || (tri.flags & kTriangleInvisible) || !(tri.normal.z > 0.0f)) continue;
        const auto z = height_in(tri, glm::vec2(p));
        if (z && *z <= p.z && (!best || *z > *best)) best = z;
    }
    return best;
}

std::optional<float> CollisionMesh::roof_above(const glm::vec3& p) const {
    auto it = cells_.find(cell_key(static_cast<std::int32_t>(std::floor(p.x / kCell)),
                                   static_cast<std::int32_t>(std::floor(p.y / kCell))));
    if (it == cells_.end()) return std::nullopt;
    std::optional<float> best;
    for (std::uint32_t t : it->second) {
        const auto& tri = triangles_[t];
        if (!probe_solid(tri) || (tri.flags & kTriangleInvisible)) continue;
        const auto z = height_in(tri, glm::vec2(p));
        if (z && *z > p.z && (!best || *z < *best)) best = z;
    }
    return best;
}

bool CollisionMesh::line_of_sight(const glm::vec3& a, const glm::vec3& b) const {
    glm::vec3 d = b - a;
    float length = glm::length(d);
    if (length < 1e-3f) return true;
    auto x0 = static_cast<std::int32_t>(std::floor(std::min(a.x, b.x) / kCell));
    auto x1 = static_cast<std::int32_t>(std::floor(std::max(a.x, b.x) / kCell));
    auto y0 = static_cast<std::int32_t>(std::floor(std::min(a.y, b.y) / kCell));
    auto y1 = static_cast<std::int32_t>(std::floor(std::max(a.y, b.y) / kCell));
    if (static_cast<std::int64_t>(x1 - x0 + 1) * (y1 - y0 + 1) > 256) return true; // too long to bother
    // Only the box's cells the segment passes over (within a unit, for rounding): a triangle it crosses is listed in
    // the cell under the crossing (index() lists a triangle in every cell its box overlaps), so the others cannot
    // change the answer. A diagonal sight line's box held up to 256 cells, most of them nowhere near it; the
    // nameplates ask this for every name in view each frame.
    const glm::vec2 from(a), dir(d);
    auto passes_over = [&](std::int32_t x, std::int32_t y) {
        constexpr float kMargin = 1.0f;
        const glm::vec2 lo(static_cast<float>(x) * kCell - kMargin, static_cast<float>(y) * kCell - kMargin);
        const glm::vec2 hi(lo.x + kCell + 2.0f * kMargin, lo.y + kCell + 2.0f * kMargin);
        float t0 = 0.0f, t1 = 1.0f; // the segment's part inside the (slab) rectangle
        for (int i = 0; i < 2; ++i) {
            if (std::abs(dir[i]) < 1e-6f) {
                if (from[i] < lo[i] || from[i] > hi[i]) return false;
                continue;
            }
            float u = (lo[i] - from[i]) / dir[i], v = (hi[i] - from[i]) / dir[i];
            if (u > v) std::swap(u, v);
            t0 = std::max(t0, u);
            t1 = std::min(t1, v);
            if (t0 > t1) return false;
        }
        return true;
    };
    for (auto x = x0; x <= x1; ++x) {
        for (auto y = y0; y <= y1; ++y) {
            if (!passes_over(x, y)) continue;
            auto it = cells_.find(cell_key(x, y));
            if (it == cells_.end()) continue;
            for (std::uint32_t t : it->second) {
                const auto& tri = triangles_[t];
                if (!probe_solid(tri)) continue;
                // Moller-Trumbore, hits strictly inside the segment.
                glm::vec3 e1 = tri.v1 - tri.v0, e2 = tri.v2 - tri.v0;
                glm::vec3 pv = glm::cross(d, e2);
                float det = glm::dot(e1, pv);
                if (std::abs(det) < 1e-8f) continue;
                float inv = 1.0f / det;
                glm::vec3 tv = a - tri.v0;
                float u = glm::dot(tv, pv) * inv;
                if (u < 0.0f || u > 1.0f) continue;
                glm::vec3 qv = glm::cross(tv, e1);
                float v = glm::dot(d, qv) * inv;
                if (v < 0.0f || u + v > 1.0f) continue;
                float hit = glm::dot(e2, qv) * inv;
                if (hit > 0.001f && hit < 0.999f) return false;
            }
        }
    }
    return true;
}

std::optional<CollisionMesh::SegmentHit> CollisionMesh::first_hit(const glm::vec3& a, const glm::vec3& b, bool back_faces) const {
    const glm::vec3 d = b - a;
    std::optional<SegmentHit> best;
    for_each_on_segment(a, b, [&](const CollisionTriangle& tri) {
        if (tri.flags & (kTriangleTransparent | 0x10)) return;
        if (!back_faces && glm::dot(tri.normal, d) > 0.0f) return; // a back face
        // Moller-Trumbore.
        glm::vec3 e1 = tri.v1 - tri.v0, e2 = tri.v2 - tri.v0;
        glm::vec3 pv = glm::cross(d, e2);
        float det = glm::dot(e1, pv);
        if (std::abs(det) < 1e-8f) return;
        float inv = 1.0f / det;
        glm::vec3 tv = a - tri.v0;
        float u = glm::dot(tv, pv) * inv;
        if (u < 0.0f || u > 1.0f) return;
        glm::vec3 qv = glm::cross(tv, e1);
        float v = glm::dot(d, qv) * inv;
        if (v < 0.0f || u + v > 1.0f) return;
        float t = glm::dot(e2, qv) * inv;
        if (t < 0.0f || t > 1.0f || (best && best->t <= t)) return;
        best = SegmentHit{t, tri.normal};
    });
    return best;
}

void CollisionMesh::for_each_on_segment(const glm::vec3& a, const glm::vec3& b,
                                        const std::function<void(const CollisionTriangle&)>& fn) const {
    if (!finite(a) || !finite(b)) return;
    std::vector<bool> seen(triangles_.size(), false);
    auto visit = [&](std::int32_t x, std::int32_t y) {
        auto it = cells_.find(cell_key(x, y));
        if (it == cells_.end()) return;
        for (std::uint32_t t : it->second) {
            if (seen[t]) continue;
            seen[t] = true;
            fn(triangles_[t]);
        }
    };
    // The cells the segment's XY projection crosses (Amanatides and Woo).
    glm::vec2 p(a.x / kCell, a.y / kCell), q(b.x / kCell, b.y / kCell);
    auto x = static_cast<std::int32_t>(std::floor(p.x)), y = static_cast<std::int32_t>(std::floor(p.y));
    auto x_end = static_cast<std::int32_t>(std::floor(q.x)), y_end = static_cast<std::int32_t>(std::floor(q.y));
    glm::vec2 d = q - p;
    int step_x = d.x > 0 ? 1 : (d.x < 0 ? -1 : 0), step_y = d.y > 0 ? 1 : (d.y < 0 ? -1 : 0);
    float t_max_x = step_x ? ((step_x > 0 ? std::floor(p.x) + 1.0f - p.x : p.x - std::floor(p.x)) / std::abs(d.x)) : 2.0f;
    float t_max_y = step_y ? ((step_y > 0 ? std::floor(p.y) + 1.0f - p.y : p.y - std::floor(p.y)) / std::abs(d.y)) : 2.0f;
    float t_dx = step_x ? 1.0f / std::abs(d.x) : 2.0f, t_dy = step_y ? 1.0f / std::abs(d.y) : 2.0f;
    for (int guard = 0; guard < 100000; ++guard) {
        visit(x, y);
        if (x == x_end && y == y_end) break;
        if (t_max_x < t_max_y) {
            if (t_max_x > 1.0f) break;
            x += step_x;
            t_max_x += t_dx;
        } else {
            if (t_max_y > 1.0f) break;
            y += step_y;
            t_max_y += t_dy;
        }
    }
    for (std::uint32_t t : big_) fn(triangles_[t]);
}

void CollisionMesh::sweep_sphere(SphereSweep& s) const { (void)sweep_sphere_hit(s); }

template <class Fn>
void CollisionMesh::for_each_near_sweep(const SphereSweep& s, Fn&& fn) const {
    // The swept sphere's box: the original walks its AABB tree with the
    // segment inflated by the radius; the grid cells under it hold the same
    // candidates (in another order: equal-t ties may pick another normal).
    glm::vec3 lo = glm::min(s.start, s.end) - glm::vec3(s.radius);
    glm::vec3 hi = glm::max(s.start, s.end) + glm::vec3(s.radius);
    if (!finite(lo) || !finite(hi)) return;
    auto x0 = static_cast<std::int32_t>(std::floor(lo.x / kCell)), x1 = static_cast<std::int32_t>(std::floor(hi.x / kCell));
    auto y0 = static_cast<std::int32_t>(std::floor(lo.y / kCell)), y1 = static_cast<std::int32_t>(std::floor(hi.y / kCell));
    if (static_cast<std::int64_t>(x1 - x0 + 1) * (y1 - y0 + 1) > 4096) return; // a sweep across the zone: not a move
    std::vector<std::uint32_t> seen;
    for (auto x = x0; x <= x1; ++x) {
        for (auto y = y0; y <= y1; ++y) {
            auto it = cells_.find(cell_key(x, y));
            if (it == cells_.end()) continue;
            for (std::uint32_t t : it->second) {
                const auto& tri = triangles_[t];
                glm::vec3 tlo = glm::min(glm::min(tri.v0, tri.v1), tri.v2), thi = glm::max(glm::max(tri.v0, tri.v1), tri.v2);
                if (tlo.x > hi.x || thi.x < lo.x || tlo.y > hi.y || thi.y < lo.y || tlo.z > hi.z || thi.z < lo.z) continue;
                // A triangle spanning several cells is listed in each of them.
                if (x1 > x0 || y1 > y0) {
                    if (std::find(seen.begin(), seen.end(), t) != seen.end()) continue;
                    seen.push_back(t);
                }
                fn(t);
            }
        }
    }
    for (std::uint32_t t : big_) fn(t);
}

std::int32_t CollisionMesh::sweep_sphere_hit(SphereSweep& s) const {
    std::int32_t hit = -1;
    for_each_near_sweep(s, [&](std::uint32_t t) {
        if (collide_triangle(s, triangles_[t])) hit = static_cast<std::int32_t>(t);
    });
    return hit;
}

bool CollisionMesh::touches_passable_ladder(const SphereSweep& s) const {
    // The DLL's model sweep (0x100c5af0) tests a passable triangle only when its flags also have bit 2: such a hit
    // short of the stop runs the hit callback (CollidingType 6 for a ladder) without stopping the sphere. Nothing
    // seen sets bit 2 on a WLD model's triangles (0x10057540 sets bit 0 alone), so this test is Hailstone's own:
    // it treats a passable ladder's triangles as if they had it. Since the sphere goes into such a ladder, a sphere
    // that already overlaps one of its triangles where the move starts (the DLL's zero-length test) touches it too;
    // without that the climber inside the rungs meets them only every other frame and gravity wins.
    bool touched = false;
    const SphereSweep still = SphereSweep::make(s.start, s.start, s.radius);
    for_each_near_sweep(s, [&](std::uint32_t t) {
        if (touched) return;
        const CollisionTriangle& tri = triangles_[t];
        constexpr std::uint8_t kPassableLadder = kTrianglePassable | kTriangleLadder;
        if ((tri.flags & kPassableLadder) != kPassableLadder) return;
        CollisionTriangle touch = tri;
        touch.flags = static_cast<std::uint8_t>(touch.flags & ~kTrianglePassable);
        SphereSweep probe = s; // tested up to the sweep's stop, which it does not move
        SphereSweep at_start = still;
        touched = collide_triangle(probe, touch) || collide_triangle(at_start, touch);
    });
    return touched;
}

} // namespace nmsc::game
