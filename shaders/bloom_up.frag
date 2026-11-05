#version 450

// Bloom, the way up: the smaller level, spread by a 3 x 3 tent, added onto this one (the pipeline blends ONE, ONE).

layout(set = 0, binding = 0) uniform sampler2D source; // the level below, half this size

layout(push_constant) uniform Push {
    vec2 texel;  // one texel of the source, in UV
    vec2 target; // one pixel of the level drawn, in UV
    uint first;  // reading the scene
} pc;

layout(location = 0) out vec4 o_color;

void main() {
    vec2 uv = gl_FragCoord.xy * pc.target;
    vec2 t = pc.texel;
    vec3 c = texture(source, uv).rgb * 4.0;
    c += (texture(source, uv + vec2(-t.x, 0.0)).rgb + texture(source, uv + vec2(t.x, 0.0)).rgb +
          texture(source, uv + vec2(0.0, -t.y)).rgb + texture(source, uv + vec2(0.0, t.y)).rgb) * 2.0;
    c += texture(source, uv - t).rgb + texture(source, uv + t).rgb + texture(source, uv + vec2(t.x, -t.y)).rgb +
         texture(source, uv + vec2(-t.x, t.y)).rgb;
    o_color = vec4(c / 16.0, 1.0);
}
