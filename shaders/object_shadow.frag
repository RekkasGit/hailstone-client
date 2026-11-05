#version 450
#extension GL_EXT_nonuniform_qualifier : require

// Doors and ground objects into a shadow cascade (render/object_renderer.cpp draw_shadow, with object_shadow.vert):
// alpha-tested parts (grates, railings) cast the shape of their texture.

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 fog_color;
    vec4 fog; // alpha_ref in z
    vec4 camera_pos;
    uint texture_index;
    uint instance;
} pc;

layout(set = 0, binding = 2) uniform sampler2D textures[];

layout(location = 0) in vec2 v_uv;

void main() {
    if (pc.fog.z > 0.0 && texture(textures[pc.texture_index], v_uv).a < pc.fog.z) discard;
}
