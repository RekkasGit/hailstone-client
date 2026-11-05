#version 450
#extension GL_GOOGLE_include_directive : require

#include "lighting.glsl"

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 fog_color;
    vec4 fog; // start, end, density
    vec4 camera_pos;
} pc;

layout(location = 0) in vec3 v_color;
layout(location = 1) in vec3 v_world;
layout(location = 2) in float v_fog; // oFog (spawn.vert)

layout(location = 0) out vec4 o_color;

void main() {
    o_color = vec4(to_linear(mix(pc.fog_color.rgb, v_color, v_fog)), 1.0);
}
