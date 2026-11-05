#version 450
#extension GL_EXT_nonuniform_qualifier : require

// A light's shadow cube (zone_point_shadow.vert): each caster's distance from the light over its radius. Alpha-tested
// geometry casts the shape of its texture; what is nearer the light than the near distance (its lamp) casts nothing.

layout(set = 0, binding = 0) uniform sampler2D textures[];

layout(push_constant) uniform Push {
    uvec4 tex;
    uvec4 tex2;
    vec4 params; // alpha_ref, ...
    vec4 slide;  // the light, its radius
    vec4 extra;  // the face scale, the near distance
    vec4 extra2;
    vec4 extra3;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec3 v_from_light;

void main() {
    if (pc.params.x > 0.0 && texture(textures[nonuniformEXT(pc.tex.x)], v_uv).a < pc.params.x) discard;
    float dist = length(v_from_light);
    if (dist < pc.extra.y) discard;
    gl_FragDepth = dist / pc.slide.w;
}
