#version 450
#extension GL_GOOGLE_include_directive : require

#include "lighting.glsl"

// Doors and ground objects into one face of a light's shadow cube (render/object_renderer.cpp draw_point_shadow):
// object.vert's model matrix; the light and its radius in fog_color, kPointShadowFaceScale, kPointShadowNear and the
// face in camera_pos.xyz.

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 fog_color;  // the light, its radius
    vec4 fog;        // alpha_ref in z
    vec4 camera_pos; // the face scale, the near distance, the face
    uint texture_index;
    uint instance; // index into the model matrices
} pc;

layout(std430, set = 0, binding = 0) readonly buffer Instances {
    mat4 models[];
};

layout(location = 0) in vec3 in_position;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec3 v_from_light;

void main() {
    v_uv = in_uv;
    v_from_light = (models[pc.instance] * vec4(in_position, 1.0)).xyz - pc.fog_color.xyz;
    gl_Position = point_shadow_clip(v_from_light, uint(pc.camera_pos.z + 0.5), pc.camera_pos.x, pc.fog_color.w);
}
