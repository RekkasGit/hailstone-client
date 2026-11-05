#version 450
#extension GL_GOOGLE_include_directive : require

// FSR 1's sharpening (AMD FidelityFX RCAS, third_party/fsr1) of fsr_easu.frag's upscale into the swapchain, with the
// dither and the encode post.frag does when it draws there itself (render/vk_context.cpp begin_overlay).

layout(set = 0, binding = 0) uniform sampler2D source;

layout(push_constant) uniform Push {
    vec4 sizes;      // unused here (fsr_easu.frag's)
    vec4 offset;     // the 3D view's place on the image (xy, pixels; /viewport)
    float sharpness; // 0 none .. 1 the most (display.sharpness)
    uint flags;
    uint frame;
    uint pad;
} pc;

layout(location = 0) out vec4 o_color;

const uint kEncodeOutput = 2u; // as post.frag's
const uint kDither = 4u;

#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"

#define FSR_RCAS_F 1
// RCAS reads one pixel round each: clamped at the picture's edges.
AF4 FsrRcasLoadF(ASU2 p) { return texelFetch(source, clamp(p, ASU2(0), textureSize(source, 0) - 1), 0); }
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}
#include "ffx_fsr1.h"

float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy - pc.offset.xy);
    vec3 c;
    if (pc.sharpness > 0.0) {
        // RCAS takes stops below its strongest: 2 at the least sharpness, 0 at the most.
        AU4 con;
        FsrRcasCon(con, 2.0 * (1.0 - clamp(pc.sharpness, 0.0, 1.0)));
        FsrRcasF(c.r, c.g, c.b, AU2(p), con);
    } else {
        c = texelFetch(source, p, 0).rgb;
    }
    if ((pc.flags & kDither) != 0u) {
        // post.frag's: triangular noise of +-1 step of 8 bits, moving each frame.
        vec2 q = gl_FragCoord.xy + float(pc.frame % 64u) * vec2(17.0, 29.0);
        c += (hash(q) + hash(q + 71.3) - 1.0) / 255.0;
    }
    c = clamp(c, 0.0, 1.0);
    // An SRGB swapchain encodes it again.
    if ((pc.flags & kEncodeOutput) == 0u) c = mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045)));
    o_color = vec4(c, 1.0);
}
