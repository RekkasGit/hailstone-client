#version 450

// Screen-space ambient occlusion (render/vk_context.h AoView), after McGuire et al.'s scalable AO: view positions
// rebuilt from the depth, the normal from the nearer neighbour on each axis, a spiral of samples within `radius`
// turned by a 4 x 4 ordered pattern (the post pass's 4 x 4 blur averages the pattern away).

layout(set = 0, binding = 0) uniform sampler2D depth_map;

layout(push_constant) uniform Push {
    vec4 projection; // tan_half_x, tan_half_y, depth terms (view depth = w / (d + z))
    vec4 params;     // radius (world units), intensity, max radius in pixels, frame
} pc;

layout(location = 0) out float o_ao;

const int kSamples = 12;
const float kTurns = 7.0;
const float kTau = 6.2831853;

ivec2 g_size;

vec3 view_pos(ivec2 p) {
    float d = texelFetch(depth_map, p, 0).r;
    float z = pc.projection.w / (d + pc.projection.z);
    vec2 ndc = (vec2(p) + 0.5) / vec2(g_size) * 2.0 - 1.0;
    // The viewport is flipped: row 0 is the top of the view.
    return vec3(ndc.x * pc.projection.x * z, -ndc.y * pc.projection.y * z, -z);
}

vec3 view_pos_clamped(ivec2 p) { return view_pos(clamp(p, ivec2(0), g_size - 1)); }

void main() {
    g_size = textureSize(depth_map, 0);
    ivec2 p = ivec2(gl_FragCoord.xy);
    if (texelFetch(depth_map, p, 0).r <= 0.0) { // the sky (reverse Z: cleared to 0)
        o_ao = 1.0;
        return;
    }
    vec3 P = view_pos(p);
    vec3 dx1 = view_pos_clamped(p + ivec2(1, 0)) - P, dx0 = P - view_pos_clamped(p - ivec2(1, 0));
    vec3 dy1 = view_pos_clamped(p + ivec2(0, 1)) - P, dy0 = P - view_pos_clamped(p - ivec2(0, 1));
    vec3 dx = abs(dx1.z) < abs(dx0.z) ? dx1 : dx0;
    vec3 dy = abs(dy1.z) < abs(dy0.z) ? dy1 : dy0;
    vec3 n = normalize(cross(dy, dx));
    if (dot(n, -P) < 0.0) n = -n;

    float radius = pc.params.x;
    float r_px = min(radius / (-P.z * pc.projection.y) * 0.5 * float(g_size.y), pc.params.z);
    if (r_px < 1.0) {
        o_ao = 1.0;
        return;
    }
    const float bayer[16] = float[](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    float angle0 = bayer[(p.x & 3) + (p.y & 3) * 4] / 16.0 * kTau;
    float r2 = radius * radius;
    float bias = 0.002 * -P.z;
    float sum = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        float a = (float(i) + 0.5) / float(kSamples);
        float theta = a * kTurns * kTau + angle0;
        ivec2 q = p + ivec2(round(vec2(cos(theta), sin(theta)) * (r_px * a)));
        if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, g_size))) continue;
        if (texelFetch(depth_map, q, 0).r <= 0.0) continue;
        vec3 v = view_pos(q) - P;
        float vv = dot(v, v);
        float f = max(r2 - vv, 0.0);
        sum += f * f * f * max((dot(v, n) - bias) / (vv + 0.01), 0.0);
    }
    o_ao = max(0.0, 1.0 - sum * pc.params.y * 5.0 / (r2 * r2 * r2 * float(kSamples)));
}
