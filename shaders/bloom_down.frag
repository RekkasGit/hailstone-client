#version 450

// Bloom (render/vk_context.h SceneOptions bloom), the way down: each level is the one above at half size, by the
// 13-tap filter of Jimenez's "Next generation post processing in Call of Duty: Advanced Warfare" (2014). The first
// level, read from the scene, weighs its five groups by 1 / (1 + luma) (Karis), so single very bright pixels do not
// flicker as the camera moves.

layout(set = 0, binding = 0) uniform sampler2D source;

layout(push_constant) uniform Push {
    vec2 texel;  // one texel of the source, in UV
    vec2 target; // one pixel of the level drawn, in UV
    uint first;  // reading the scene
} pc;

layout(location = 0) out vec4 o_color;

float karis(vec3 c) { return 1.0 / (1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722))); }

vec3 group(vec3 a, vec3 b, vec3 c, vec3 d, out float w) {
    vec3 s = (a + b + c + d) * 0.25;
    w = pc.first != 0u ? karis(s) : 1.0;
    return s * w;
}

void main() {
    vec2 uv = gl_FragCoord.xy * pc.target;
    vec2 t = pc.texel;
    vec3 a = texture(source, uv + t * vec2(-2.0, -2.0)).rgb;
    vec3 b = texture(source, uv + t * vec2(0.0, -2.0)).rgb;
    vec3 c = texture(source, uv + t * vec2(2.0, -2.0)).rgb;
    vec3 d = texture(source, uv + t * vec2(-2.0, 0.0)).rgb;
    vec3 e = texture(source, uv).rgb;
    vec3 f = texture(source, uv + t * vec2(2.0, 0.0)).rgb;
    vec3 g = texture(source, uv + t * vec2(-2.0, 2.0)).rgb;
    vec3 h = texture(source, uv + t * vec2(0.0, 2.0)).rgb;
    vec3 i = texture(source, uv + t * vec2(2.0, 2.0)).rgb;
    vec3 j = texture(source, uv + t * vec2(-1.0, -1.0)).rgb;
    vec3 k = texture(source, uv + t * vec2(1.0, -1.0)).rgb;
    vec3 l = texture(source, uv + t * vec2(-1.0, 1.0)).rgb;
    vec3 m = texture(source, uv + t * vec2(1.0, 1.0)).rgb;
    // The centre box counts half, the four corner boxes an eighth each.
    float w0, w1, w2, w3, w4;
    vec3 sum = group(j, k, l, m, w0) * 0.5 + group(a, b, d, e, w1) * 0.125 + group(b, c, e, f, w2) * 0.125 +
               group(d, e, g, h, w3) * 0.125 + group(e, f, h, i, w4) * 0.125;
    float weight = w0 * 0.5 + (w1 + w2 + w3 + w4) * 0.125;
    // Guard against NaN or infinity from the scene (a single bad pixel would spread over the whole chain).
    vec3 color = sum / weight;
    if (any(isnan(color)) || any(isinf(color))) color = vec3(0.0);
    o_color = vec4(min(color, vec3(256.0)), 1.0);
}
