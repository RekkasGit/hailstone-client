#version 450

// Doors and ground objects into a shadow cascade (render/object_renderer.cpp draw_shadow): object.vert's model
// matrix, with the cascade's matrix as view_proj.

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 fog_color;
    vec4 fog; // alpha_ref in z
    vec4 camera_pos;
    uint texture_index;
    uint instance; // index into the model matrices
} pc;

layout(std430, set = 0, binding = 0) readonly buffer Instances {
    mat4 models[];
};

layout(location = 0) in vec3 in_position;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec2 v_uv;

void main() {
    v_uv = in_uv;
    gl_Position = pc.view_proj * (models[pc.instance] * vec4(in_position, 1.0));
}
