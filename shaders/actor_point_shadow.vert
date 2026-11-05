#version 450
#extension GL_GOOGLE_include_directive : require

#include "lighting.glsl"

// Characters into one face of a light's shadow cube (render/actor_renderer.cpp draw_point_shadow): actor.vert's
// skinning; the light and its radius in fog_color, the face in tint, kPointShadowFaceScale and kPointShadowNear in
// camera_pos.xy.

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 fog_color; // the light, its radius
    vec4 fog;       // alpha_ref in z
    vec4 camera_pos; // the face scale, the near distance
    uint texture_index;
    uint bone_offset;
    uint tint; // the face
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
layout(location = 1) out vec3 v_from_light;

void main() {
    mat4 bone = bones[pc.bone_offset + in_bones.x] * in_weights.x;
    if (in_weights.y > 0.0) bone += bones[pc.bone_offset + in_bones.y] * in_weights.y;
    if (in_weights.z > 0.0) bone += bones[pc.bone_offset + in_bones.z] * in_weights.z;
    if (in_weights.w > 0.0) bone += bones[pc.bone_offset + in_bones.w] * in_weights.w;
    v_uv = in_uv;
    v_from_light = (bone * vec4(in_position, 1.0)).xyz - pc.fog_color.xyz;
    gl_Position = point_shadow_clip(v_from_light, pc.tint, pc.camera_pos.x, pc.fog_color.w);
}
