#version 450

// Classic sky layers (src/render/sky_renderer.cpp). Drawn around the camera
// with the view rotation only, pinned to the far plane (reverse Z: depth 0).

layout(push_constant) uniform Push {
    mat4 view_proj;  // projection * view rotation (no translation)
    vec4 tint;       // rgb multiplier, a = opacity
    vec2 uv_offset;  // cloud drift
    uint texture_index;
    uint use_table;  // 1: colour from the sky colour table entry, 0: white
    vec4 haze;       // the modern mode's horizon haze (linear rgb, a: on)
    vec4 light;      // a > 0: the colour is scaled by rgb (the modern clouds' light); a 2: keyed clouds
} pc;

layout(set = 0, binding = 1) uniform SkyColors {
    vec4 colors[18]; // the sky colour table at the current sky level
} sky;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in uint in_entry;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec3 v_color;
layout(location = 2) out vec3 v_dir; // the dome and clouds are unit directions around the camera

void main() {
    v_uv = in_uv + pc.uv_offset;
    v_dir = in_position;
    v_color = pc.use_table != 0u ? sky.colors[min(in_entry, 17u)].rgb : vec3(1.0);
    vec4 p = pc.view_proj * vec4(in_position, 1.0);
    p.z = 0.0;
    gl_Position = p;
}
