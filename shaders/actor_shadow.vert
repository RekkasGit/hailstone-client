#version 450

// Characters into a shadow cascade (render/actor_renderer.cpp draw_shadow): actor.vert's skinning, with the
// cascade's matrix as view_proj.

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 fog_color;
    vec4 fog;
    vec4 camera_pos;
    uint texture_index;
    uint bone_offset;
    uint tint;
    uint texture2;
} pc;

layout(std430, set = 0, binding = 0) readonly buffer Bones {
    mat4 bones[];
};

layout(location = 0) in vec3 in_position;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in uvec4 in_bones;
layout(location = 4) in vec4 in_weights;

layout(location = 0) out vec2 v_uv;

void main() {
    mat4 bone = bones[pc.bone_offset + in_bones.x] * in_weights.x;
    if (in_weights.y > 0.0) bone += bones[pc.bone_offset + in_bones.y] * in_weights.y;
    if (in_weights.z > 0.0) bone += bones[pc.bone_offset + in_bones.z] * in_weights.z;
    if (in_weights.w > 0.0) bone += bones[pc.bone_offset + in_bones.w] * in_weights.w;
    v_uv = in_uv;
    gl_Position = pc.view_proj * (bone * vec4(in_position, 1.0));
}
