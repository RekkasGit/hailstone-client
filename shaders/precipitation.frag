#version 450

// A precipitation particle (shaders/precipitation.vert): its texture's frame times the sky's colour for it, alpha
// blended.

layout(set = 0, binding = 0) uniform sampler2D textures[16];

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 camera;
    vec4 forward;
    uvec4 texture_slot;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 o_color;

void main() {
    vec4 t = texture(textures[pc.texture_slot.x], v_uv);
    float a = t.a * v_color.a;
    if (a <= 0.004) discard;
    o_color = vec4(t.rgb * v_color.rgb, a);
}
