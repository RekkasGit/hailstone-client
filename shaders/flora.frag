#version 450
#extension GL_EXT_nonuniform_qualifier : require

// The radial flora (render/flora_renderer.h, flora.vert): the picture cut out at half alpha, in the light the CPU gave the plant, into the haze.

layout(set = 0, binding = 0) uniform sampler2D textures[];

layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in vec3 v_light;
layout(location = 2) flat in vec4 v_haze;
layout(location = 3) flat in uint v_texture;

layout(location = 0) out vec4 o_color;

vec3 to_linear(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045))); }

void main() {
    vec4 c = texture(textures[nonuniformEXT(v_texture)], v_uv);
    if (c.a < 0.5) discard;
    vec3 lit = to_linear(c.rgb) * v_light;
    o_color = vec4(mix(v_haze.rgb, lit, v_haze.w), 1.0);
}
