#version 450
#extension GL_EXT_nonuniform_qualifier : require

// Characters into a light's shadow cube (actor_point_shadow.vert): their distance from the light over its radius;
// alpha-tested parts (hair, cloaks) cast the shape of their texture.

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 fog_color; // the light, its radius
    vec4 fog;       // alpha_ref in z
    vec4 camera_pos; // the face scale, the near distance
    uint texture_index;
    uint bone_offset;
    uint tint;
    uint texture2;
} pc;

layout(set = 0, binding = 2) uniform sampler2D textures[];

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec3 v_from_light;

void main() {
    if (pc.fog.z > 0.0 && texture(textures[pc.texture_index], v_uv).a < pc.fog.z) discard;
    float dist = length(v_from_light);
    if (dist < pc.camera_pos.y) discard;
    gl_FragDepth = dist / pc.fog_color.w;
}
