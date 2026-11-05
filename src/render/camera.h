#pragma once

// Free-flying camera in EQ world space (right-handed, Z up).

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "game/view_distance.h"
#include "render/shadows.h"
#include "render/vk_context.h"

namespace nmsc::render {

struct FlyCamera {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;   // radians, 0 looks along +X, positive turns toward +Y
    float pitch = 0.0f; // radians, positive looks up
    // The original's camera FOV in 512ths of a circle (90 for every stock
    // camera) and near plane (game/view_distance.h, camera_fov).
    float fov_units = game::kStockFovUnits;
    float near_plane = game::kCameraNear;

    glm::vec3 forward() const {
        return {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
    }
    glm::vec3 right() const { return glm::normalize(glm::cross(forward(), glm::vec3(0, 0, 1))); }

    void rotate(float d_yaw, float d_pitch) {
        yaw += d_yaw;
        pitch = std::clamp(pitch + d_pitch, -1.55f, 1.55f);
    }

    glm::mat4 view() const { return glm::lookAtRH(position, position + forward(), glm::vec3(0, 0, 1)); }

    // The original's D3DXMatrixPerspectiveRH(2 tan(h/2), w / (h/v), near, far)
    // for a width x height viewport, as a reverse-Z projection with Vulkan's
    // 0..1 depth range (near maps to 1, far to 0). The depth mapping differs
    // from D3D's; the image does not.
    // The view as the shadow cascades take it (render/shadows.h).
    ShadowCamera shadow_camera(int width, int height) const {
        auto fov = game::camera_fov(fov_units, width, height);
        ShadowCamera c;
        c.position = position;
        c.forward = forward();
        c.right = right();
        c.up = glm::cross(c.right, c.forward);
        c.tan_half_x = fov.tan_half_x;
        c.tan_half_y = fov.tan_half_y;
        c.near_plane = near_plane;
        return c;
    }

    // The projection terms the screen-space AO rebuilds positions with (render/vk_context.h AoView).
    AoView ao_view(int width, int height, float far_plane) const {
        auto fov = game::camera_fov(fov_units, width, height);
        AoView v;
        v.tan_half_x = fov.tan_half_x;
        v.tan_half_y = fov.tan_half_y;
        v.near_plane = near_plane;
        v.far_plane = far_plane;
        return v;
    }

    glm::mat4 projection(int width, int height, float far_plane) const {
        auto fov = game::camera_fov(fov_units, width, height);
        float n = near_plane;
        glm::mat4 p(0.0f);
        p[0][0] = 1.0f / fov.tan_half_x;
        p[1][1] = 1.0f / fov.tan_half_y;
        p[2][2] = n / (far_plane - n);
        p[2][3] = -1.0f;
        p[3][2] = far_plane * n / (far_plane - n);
        return p;
    }
};

} // namespace nmsc::render
