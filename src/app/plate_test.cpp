#include "app/plate_test.h"

#include <algorithm>

namespace nmsc::app {

std::optional<float> surface_depth(const game::CollisionMesh& collision, const ViewRays& v, float px, float py) {
    // The ray through the scene pixel: forward plus the sideways and upward parts, so a point `s` along it lies `s`
    // ahead of the camera (its view depth).
    const float nx = px / static_cast<float>(v.width) * 2.0f - 1.0f;
    const float ny = 1.0f - py / static_cast<float>(v.height) * 2.0f;
    const glm::vec3 dir = v.forward + v.right * (nx * v.tan_half_x) + v.up * (ny * v.tan_half_y);
    const auto hit = collision.first_hit(v.position, v.position + dir * v.far_plane, false);
    if (!hit) return std::nullopt;
    return hit->t * v.far_plane;
}

std::vector<TestPlate> make_test_plates(const game::CollisionMesh& collision, const ViewRays& v, int columns, int rows,
                                        float box_w, float box_h) {
    constexpr int kAcross = 5, kDown = 3; // the points of the box whose surfaces decide its depth
    std::vector<TestPlate> out;
    int n = 0;
    for (int gy = 0; gy < rows; ++gy) {
        for (int gx = 0; gx < columns; ++gx, ++n) {
            TestPlate p;
            p.id = n + 1;
            p.kind = static_cast<PlateKind>(n % 3);
            p.top_middle = {(static_cast<float>(gx) + 0.5f) / static_cast<float>(columns) * static_cast<float>(v.width),
                            (static_cast<float>(gy) + 0.5f) / static_cast<float>(rows) * static_cast<float>(v.height) - box_h * 0.5f};
            float near_hit = 1e30f, far_hit = 0.0f;
            bool all = true;
            for (int i = 0; i < kAcross * kDown && all; ++i) {
                const float u = static_cast<float>(i % kAcross) / static_cast<float>(kAcross - 1);
                const float w = static_cast<float>(i / kAcross) / static_cast<float>(kDown - 1);
                const auto d = surface_depth(collision, v, p.top_middle.x - box_w * 0.5f + box_w * u, p.top_middle.y + box_h * w);
                if (!d || *d < kPlateMinHit) {
                    all = false;
                } else {
                    near_hit = std::min(near_hit, *d);
                    far_hit = std::max(far_hit, *d);
                }
            }
            if (!all) continue;
            if (p.kind == PlateKind::Front) p.depth = near_hit - kPlateSpacing;
            else if (p.kind == PlateKind::Behind) p.depth = far_hit + kPlateSpacing;
            else if (far_hit - near_hit > 2.0f * kPlateSpacing) p.depth = (near_hit + far_hit) * 0.5f;
            else continue;
            out.push_back(p);
        }
    }
    return out;
}

} // namespace nmsc::app
