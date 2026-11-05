#version 450
#extension GL_GOOGLE_include_directive : require

#include "lighting.glsl"

// The haze, or the water round the camera under it, behind everything (render/zone_renderer.h draw_backdrop): one triangle over the screen at the
// far plane (reverse Z: depth 0), each corner's view direction back through the frame's view_proj.

layout(set = 1, binding = 0) uniform Scene {
    mat4 view_proj;
    vec4 fog_color;
    vec4 fog;
    vec4 camera_pos;
    SCENE_LIGHTING_FIELDS
} scene;

layout(location = 0) out vec3 v_dir;

void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    mat4 inv = inverse(scene.view_proj);
    vec4 far = inv * vec4(p, 0.0, 1.0);
    vec4 near = inv * vec4(p, 1.0, 1.0);
    v_dir = far.xyz / far.w - near.xyz / near.w;
    gl_Position = vec4(p, 0.0, 1.0);
}
