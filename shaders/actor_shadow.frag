#version 450
#extension GL_EXT_nonuniform_qualifier : require

// Characters into a shadow cascade (render/actor_renderer.cpp draw_shadow, with actor_shadow.vert): alpha-tested parts
// (hair, cloaks) cast the shape of their texture.

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 fog_color;
    vec4 fog; // start, end, alpha_ref, opacity
    vec4 camera_pos;
    uint texture_index;
    uint bone_offset;
    uint tint;
    uint texture2;
} pc;

layout(set = 0, binding = 2) uniform sampler2D textures[];

layout(location = 0) in vec2 v_uv;

void main() {
    if (pc.fog.z > 0.0 && texture(textures[pc.texture_index], v_uv).a < pc.fog.z) discard;
}
