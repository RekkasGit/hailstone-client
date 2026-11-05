#version 450
#extension GL_GOOGLE_include_directive : require

#include "lighting.glsl"

// The modern haze behind everything where no sky is drawn (render/zone_renderer.h draw_backdrop): what apply_haze
// gives a point infinitely far away in that direction, the haze's colour and its glow towards the sun, so the gaps
// between the leaves and the world past the far plane match what the haze makes of the trees in front. With the
// camera under water it is the water (render::set_underwater; no glow there): the surface overhead carried on past
// its polygons, and the light shafts in it (lighting.glsl underwater_shafts), as the surfaces in front take them.

layout(set = 1, binding = 0) uniform Scene {
    mat4 view_proj;
    vec4 fog_color;
    vec4 fog;
    vec4 camera_pos;
    SCENE_LIGHTING_FIELDS
} scene;

layout(set = 2, binding = 3) uniform sampler2DArray water_map; // the shadow map's depths: the water map's

layout(location = 0) in vec3 v_dir;
layout(location = 0) out vec4 o_color;

void main() {
    vec3 dir = normalize(v_dir);
    vec3 p = scene.camera.xyz + dir * 1000.0;
    // Looking up, the surface goes on past the water's own polygons: darker the nearer it is, through the haze.
    vec3 color = scene.haze_color.rgb + scene.light.rgb * (pow(max(dot(dir, scene.to_light.xyz), 0.0), 6.0) * scene.haze.w * 0.25);
    float edge;
    float depth = water_depth(water_map, scene.cover, scene.cover_params, scene.camera.xyz, edge);
    if (depth > 0.0 && dir.z > 0.01) {
        float x = (depth / dir.z) * scene.haze.x;
        color = mix(color, color * 0.55, exp(-3.5 * x * x));
    }
    color += underwater_shafts(water_map, scene.cover, scene.cover_params, scene.camera.xyz, p, scene.to_light,
                                   scene.light, scene.haze_color.rgb, scene.time.x, caustic_hash(gl_FragCoord.xy));
    o_color = vec4(color, 1.0);
}
