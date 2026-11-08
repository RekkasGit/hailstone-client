#include "render/culling.h"

namespace hailstone::render {

Frustum frustum_from(const glm::mat4& m) {
    // Row i of the matrix (glm is column-major): clip.i = dot(row_i, (p, 1)).
    auto row = [&](int i) { return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
    const glm::vec4 x = row(0), y = row(1), z = row(2), w = row(3);
    Frustum f;
    f.planes = {w + x, w - x, w + y, w - y, z, w - z};
    return f;
}

bool box_in_frustum(const Frustum& f, const Box& b) {
    for (const glm::vec4& p : f.planes) {
        // The box's corner furthest along the plane's normal (the "positive vertex").
        glm::vec3 far(p.x >= 0.0f ? b.max.x : b.min.x, p.y >= 0.0f ? b.max.y : b.min.y,
                      p.z >= 0.0f ? b.max.z : b.min.z);
        if (p.x * far.x + p.y * far.y + p.z * far.z + p.w < 0.0f) return false;
    }
    return true;
}

bool boxes_overlap(const Box& a, const Box& b) {
    return a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y &&
           a.min.z <= b.max.z && b.min.z <= a.max.z;
}

} // namespace hailstone::render
