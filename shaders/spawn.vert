#version 450

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 fog_color;
    vec4 fog; // start, end, density
    vec4 camera_pos;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_model0;
layout(location = 3) in vec4 in_model1;
layout(location = 4) in vec4 in_model2;
layout(location = 5) in vec4 in_model3;
layout(location = 6) in vec4 in_color;

layout(location = 0) out vec3 v_color;
layout(location = 1) out vec3 v_world;
layout(location = 2) out float v_fog;

// The effects' fog (RegionC1 vs_1_1): per
// vertex, f = clamp((|eye - pos| - FogStart) FogRangeInv fFogRange, 0, fFogRange)
// with FogRangeInv = 1 / (end - start), fFogRange = 10, and
// oFog = exp(-(f FogDensity)^2) (1: no fog); the pixel is lerp(fog, colour, oFog).
float fog_factor(vec3 p) {
    float dist = length(pc.camera_pos.xyz - p);
    float range_inv = 1.0 / max(pc.fog.y - pc.fog.x, 1.0);
    float f = clamp((dist - pc.fog.x) * range_inv * 10.0, 0.0, 10.0);
    float x = f * pc.fog.z;
    return exp(-(x * x));
}

void main() {
    mat4 model = mat4(in_model0, in_model1, in_model2, in_model3);
    vec4 world = model * vec4(in_position, 1.0);
    vec3 n = normalize(mat3(model) * in_normal);
    float light = 0.55 + 0.45 * max(dot(n, normalize(vec3(0.4, 0.3, 0.85))), 0.0);
    v_color = in_color.rgb * light;
    v_world = world.xyz;
    v_fog = fog_factor(world.xyz);
    gl_Position = pc.view_proj * world;
}
