#version 450
#extension GL_GOOGLE_include_directive : require

#include "lighting.glsl"

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 tint;
    vec2 uv_offset;
    uint texture_index;
    uint use_table;
    vec4 haze; // the modern mode's horizon haze (linear rgb, a: on)
    vec4 light; // a > 0: the colour (linear) is scaled by rgb: the modern clouds, lit by the sun and sky; a 2: a
                // cloud layer with its sky painted in, keyed: the white is cloud, the blue is let through
} pc;

layout(set = 0, binding = 0) uniform sampler2D textures[12]; // SkyRenderer::kMaxTextures

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec3 v_color;
layout(location = 2) in vec3 v_dir;

layout(location = 0) out vec4 o_color;

// No fog on the sky: the original's outer sky pass turns fog off
// (OpenEQ SKY_DOME_DRAW_STATE.md, FOGENABLE 0).
void main() {
    vec4 tex = texture(textures[min(pc.texture_index, 11u)], v_uv);
    vec3 color = to_linear(tex.rgb * v_color * pc.tint.rgb); // the scene target is linear
    float alpha = tex.a * pc.tint.a;
    if (pc.light.a > 1.5) {
        // Cloud where red (absent from the painted blue) is high; its shade from the brightness. A storm (the
        // fraction over 2) spreads the cloud over the sky.
        float storm = pc.light.a - 2.0;
        float cloud = mix(smoothstep(0.42, 0.85, tex.r), 1.0, storm * 0.85);
        // Thinning out towards the horizon, where the painted layer is stretched into streaks: the atmosphere's
        // own horizon shows there.
        alpha = cloud * smoothstep(0.03, 0.3, normalize(v_dir).z);
        color = vec3(to_linear(vec3(dot(tex.rgb, vec3(0.3, 0.5, 0.2)))).r);
    }
    if (pc.light.a > 0.0) {
        color *= pc.light.rgb;
        // The modern clouds (the layer and the patches) thin out low over the horizon.
        if (pc.light.a < 1.5) alpha *= smoothstep(0.0, 0.2, normalize(v_dir).z);
    }
    if (pc.haze.a > 0.0) {
        // Into the haze at and below the horizon, clear about 7 degrees up, so the distant land meets the sky.
        float up = v_dir.z / max(length(v_dir), 1e-4);
        float h = 1.0 - smoothstep(-0.02, 0.12, up);
        color = mix(color, pc.haze.rgb, h * h * pc.haze.a);
    }
    o_color = vec4(color, alpha);
}
