#version 450

// The modern lighting's fire (render/fire_renderer.h): one flame per instance, on a quad turned about the vertical to
// face the camera. The quad runs 1.4 half-widths either side of the flame's axis and from a little under its foot to
// 1.7 heights up, room for the tongues to sway and the embers to rise; it stands a little towards the camera, so the
// logs or the lamp it burns in do not cut it (the fragment shader fades it against them instead).

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 camera_time; // xyz the camera, w seconds
    vec4 screen;      // the depth copy's terms (SceneLighting::screen)
    vec4 haze;        // SceneLighting::haze xyz, w the glow
} pc;

layout(location = 0) in vec4 i_base_height; // xyz the foot, w the height
layout(location = 1) in vec4 i_shape;       // x the width, y the seed, z the intensity
layout(location = 2) in vec4 i_tint;        // rgb a magic flame's hue, w how far it takes the fire's own colours

layout(location = 0) out vec2 v_uv;          // x in half-widths from the axis, y in heights from the foot
layout(location = 1) out vec3 v_world;
layout(location = 2) flat out vec4 v_shape;  // x the half-width, y the height (world), z the seed, w the intensity
layout(location = 3) flat out vec4 v_tint;

const float kSide = 1.4;
const float kBelow = -0.1;
const float kTop = 1.7;

void main() {
    const vec2 corners[6] = vec2[6](vec2(-1, 0), vec2(1, 0), vec2(1, 1), vec2(-1, 0), vec2(1, 1), vec2(-1, 1));
    vec2 c = corners[gl_VertexIndex];
    vec3 foot = i_base_height.xyz;
    float height = i_base_height.w;
    float half_width = i_shape.x * 0.5;
    vec3 to_camera = pc.camera_time.xyz - foot;
    to_camera.z = 0.0;
    float len = length(to_camera);
    vec3 toward = len > 1e-3 ? to_camera / len : vec3(1.0, 0.0, 0.0);
    vec3 right = vec3(-toward.y, toward.x, 0.0);
    float y = mix(kBelow, kTop, c.y);
    vec3 p = foot + right * (c.x * kSide * half_width) + vec3(0.0, 0.0, y * height) + toward * (half_width * 0.6);
    v_uv = vec2(c.x * kSide, y);
    v_world = p;
    v_shape = vec4(half_width, height, i_shape.y, i_shape.z);
    v_tint = i_tint;
    gl_Position = pc.view_proj * vec4(p, 1.0);
}
