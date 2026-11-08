#include "render/shadows.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace hailstone::render {

ShadowCascades fit_shadow_cascades(const ShadowCamera& camera, const glm::vec3& to_light, float distance,
                                   float caster_reach, std::uint32_t map_size) {
    ShadowCascades out;
    // The practical split scheme: mostly logarithmic, so the near cascades are small and sharp.
    const float n = std::max(camera.near_plane, 2.0f);
    const float f = std::max(distance, n + 1.0f);
    constexpr float kLambda = 0.8f;
    std::array<float, kShadowCascades + 1> depth{};
    depth[0] = camera.near_plane;
    for (std::uint32_t i = 1; i <= kShadowCascades; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kShadowCascades);
        depth[i] = kLambda * n * std::pow(f / n, t) + (1.0f - kLambda) * (n + (f - n) * t);
    }

    glm::vec3 up = std::abs(to_light.z) > 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
    for (std::uint32_t c = 0; c < kShadowCascades; ++c) {
        // The slice's corners, and the sphere around them (its size does not change as the camera turns).
        glm::vec3 corners[8];
        int k = 0;
        for (float d : {depth[c], depth[c + 1]})
            for (float sx : {-1.0f, 1.0f})
                for (float sy : {-1.0f, 1.0f})
                    corners[k++] = camera.position + camera.forward * d + camera.right * (sx * d * camera.tan_half_x) +
                                   camera.up * (sy * d * camera.tan_half_y);
        glm::vec3 center(0.0f);
        for (const auto& p : corners) center += p;
        center /= 8.0f;
        float radius = 0.0f;
        for (const auto& p : corners) radius = std::max(radius, glm::length(p - center));
        radius = std::ceil(radius * 16.0f) / 16.0f;

        float back = radius + caster_reach;
        glm::mat4 view = glm::lookAtRH(center + to_light * back, center, up);
        glm::mat4 proj = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.0f, back + radius);
        glm::mat4 vp = proj * view;
        // Whole texels: move the projection so the world origin lands on a texel corner.
        glm::vec4 origin = vp * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        float half = static_cast<float>(map_size) * 0.5f;
        glm::vec2 texel(origin.x * half, origin.y * half);
        glm::vec2 offset = (glm::round(texel) - texel) / half;
        glm::mat4 snap = glm::translate(glm::mat4(1.0f), glm::vec3(offset, 0.0f));
        out.view_proj[c] = snap * vp;
        out.splits[static_cast<int>(c)] = depth[c + 1];
        out.texel_world[static_cast<int>(c)] = 2.0f * radius / static_cast<float>(map_size);
    }
    return out;
}

} // namespace hailstone::render
