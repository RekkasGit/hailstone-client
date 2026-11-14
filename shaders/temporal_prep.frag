#version 450
#extension GL_GOOGLE_include_directive : require

// The temporal upscaler's inputs at the scene's size (render/vk_context.cpp begin_overlay; docs/graphics.md
// "Temporal upscaling"): the scene with its AO applied (the AO belongs to the lighting, before the resolve, so the
// history smooths its pattern), and each pixel's motion: where the surface it shows was a frame ago, in UV.
//
// The motion is the camera's alone, rebuilt from the depth (render/temporal.h reprojection). What moves on its own
// (characters, doors, swaying plants) is followed only as far as the camera moved; taa_resolve.frag's clip keeps
// it from ghosting.

layout(set = 0, binding = 0) uniform sampler2D scene;
layout(set = 0, binding = 1) uniform sampler2D ao_map;
layout(set = 0, binding = 2) uniform sampler2D depth_map;

layout(push_constant) uniform Push {
    mat4 reproject; // this frame's clip space (unjittered) to the last frame's
    vec4 depth;     // view depth = y / (d + x); z: the AO's strength; w: 1 to apply the AO
    vec4 jitter;    // xy: this frame's jitter in NDC (render/temporal.h jitter_ndc)
} pc;

layout(location = 0) out vec4 o_color;
layout(location = 1) out vec2 o_motion;

#include "ao_blur.glsl"

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec3 c = texelFetch(scene, p, 0).rgb;
    float d = texelFetch(depth_map, p, 0).r;
    if (pc.depth.w > 0.0 && d > 0.0) c *= mix(1.0, blurred_ao(ao_map, depth_map, p, pc.depth.xy), pc.depth.z);
    o_color = vec4(c, 1.0);

    // The pixel's centre in NDC (the viewport is flipped: y up), less the jitter: where the surface lies unjittered.
    vec2 size = vec2(textureSize(depth_map, 0));
    vec2 uv = gl_FragCoord.xy / size;
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0) - pc.jitter.xy;
    vec4 prev = pc.reproject * vec4(ndc, d, 1.0);
    if (prev.w <= 1e-6) {
        // Behind last frame's camera: nothing to follow. Far outside the screen, so the resolve drops its history.
        o_motion = vec2(4.0);
        return;
    }
    vec2 moved = prev.xy / prev.w - ndc;
    o_motion = vec2(moved.x * 0.5, -moved.y * 0.5);
}
