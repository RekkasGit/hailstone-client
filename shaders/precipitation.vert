#version 450

// The precipitation manager's particles (render/weather_renderer.h, game/precipitation.h), one instance each, as the
// original's CRender builds their quads (0x1007fd10): orientation 0 a Width x Height square facing the camera,
// 1 a streak Height long standing up from the drop (against the wind (0, 0, -1)), Width across, 2 lying flat on the
// ground. Facing and upright ones are at least a pixel wide, as the original's.

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 camera;  // xyz, w: a pixel's world size one unit in front of the camera
    vec4 forward; // xyz the camera's direction, w: the light scale
    uvec4 texture_slot;
} pc;

layout(location = 0) in vec4 i_position; // xyz, w: the orientation
layout(location = 1) in vec4 i_size;     // x width, y height
layout(location = 2) in vec4 i_uv;       // u0, v0, u1, v1
layout(location = 3) in vec4 i_color;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

void main() {
    const vec2 kCorners[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
    vec2 corner = kCorners[gl_VertexIndex];
    vec3 p = i_position.xyz;
    int orientation = int(i_position.w + 0.5);
    float depth = max(dot(p - pc.camera.xyz, pc.forward.xyz), 1e-3);
    float width = i_size.x, height = i_size.y;
    if (orientation != 2) width = max(width, pc.camera.w * depth); // a pixel at least
    vec3 world;
    vec3 to_cam = normalize(pc.camera.xyz - p);
    if (orientation == 1) {
        // From the drop up its streak.
        vec3 axis = vec3(0.0, 0.0, 1.0);
        vec3 side = cross(axis, to_cam);
        float len = length(side);
        side = len > 1e-4 ? side / len : vec3(1.0, 0.0, 0.0);
        world = p + side * (corner.x * 0.5 * width) + axis * ((corner.y * 0.5 + 0.5) * height);
    } else if (orientation == 2) {
        world = p + vec3(corner.x * 0.5 * width, corner.y * 0.5 * height, 0.0);
    } else {
        vec3 right = cross(pc.forward.xyz, vec3(0.0, 0.0, 1.0));
        float len = length(right);
        right = len > 1e-4 ? right / len : vec3(1.0, 0.0, 0.0);
        vec3 up = cross(right, pc.forward.xyz);
        world = p + right * (corner.x * 0.5 * width) + up * (corner.y * 0.5 * height);
    }
    v_uv = vec2(mix(i_uv.x, i_uv.z, corner.x * 0.5 + 0.5), mix(i_uv.w, i_uv.y, corner.y * 0.5 + 0.5));
    v_color = vec4(i_color.rgb * pc.forward.w, i_color.a);
    gl_Position = pc.view_proj * vec4(world, 1.0);
}
