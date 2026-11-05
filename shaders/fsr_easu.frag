#version 450
#extension GL_GOOGLE_include_directive : require

// FSR 1's edge-adaptive upscale (AMD FidelityFX EASU, third_party/fsr1): the post pass's picture at the render scale
// (display.render_scale), tonemapped and sRGB-encoded as EASU wants it, up to the 3D view's size
// (render/vk_context.cpp begin_overlay).

layout(set = 0, binding = 0) uniform sampler2D source;

layout(push_constant) uniform Push {
    vec4 sizes;  // the source's width and height, the output's width and height (pixels)
    vec4 offset; // unused here (fsr_rcas.frag's)
    float sharpness;
    uint flags;
    uint frame;
    uint pad;
} pc;

layout(location = 0) out vec4 o_color;

#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"

#define FSR_EASU_F 1
AF4 FsrEasuRF(AF2 p) { return textureGather(source, p, 0); }
AF4 FsrEasuGF(AF2 p) { return textureGather(source, p, 1); }
AF4 FsrEasuBF(AF2 p) { return textureGather(source, p, 2); }
#include "ffx_fsr1.h"

void main() {
    AU4 con0, con1, con2, con3;
    FsrEasuCon(con0, con1, con2, con3, pc.sizes.x, pc.sizes.y, pc.sizes.x, pc.sizes.y, pc.sizes.z, pc.sizes.w);
    AF3 c;
    FsrEasuF(c, AU2(gl_FragCoord.xy), con0, con1, con2, con3);
    o_color = vec4(c, 1.0);
}
