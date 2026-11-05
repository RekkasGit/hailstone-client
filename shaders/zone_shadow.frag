#version 450
#extension GL_EXT_nonuniform_qualifier : require

// Alpha-tested zone geometry (leaves, fences) casts the shape of its texture (zone_shadow.vert).

layout(set = 0, binding = 0) uniform sampler2D textures[];

layout(push_constant) uniform Push {
    uvec4 tex;
    uvec4 tex2;
    vec4 params; // alpha_ref, ...
    vec4 slide;
    vec4 extra;
    vec4 extra2;
    vec4 extra3;
} pc;

layout(location = 0) in vec2 v_uv;

void main() {
    if (pc.params.x > 0.0 && texture(textures[nonuniformEXT(pc.tex.x)], v_uv).a < pc.params.x) discard;
}
