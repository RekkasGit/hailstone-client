#version 450

// Particles (render/particle_renderer.cpp): camera-facing quads built on the CPU, one texture per quad.

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 screen;
    vec4 params;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) in uint in_texture;
layout(location = 4) in vec4 in_shade;
layout(location = 5) in vec4 in_haze;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(location = 2) flat out uint v_texture;
layout(location = 3) out vec4 v_shade;
layout(location = 4) out vec4 v_haze;

void main() {
    v_uv = in_uv;
    v_color = in_color;
    v_texture = in_texture;
    v_shade = in_shade;
    v_haze = in_haze;
    gl_Position = pc.view_proj * vec4(in_position, 1.0);
}
