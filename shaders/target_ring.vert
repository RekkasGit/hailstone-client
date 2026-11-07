#version 450

// The target ring (render/target_ring_renderer.h): a box round the ring, from below its feet to above them, that
// target_ring.frag paints the ring through. Each face carries its outward normal so the fragment shader keeps the far
// faces only.

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 camera_time; // xyz the camera, w seconds
    vec4 screen;      // x, y the depth terms, zw the texel size
    vec4 haze;
    vec4 forward;     // xyz the camera's forward
} pc;

layout(location = 0) in vec4 i_feet;  // xyz the feet, w the radius
layout(location = 1) in vec4 i_color; // display colour, alpha
layout(location = 2) in vec4 i_shape; // x the half width, y the spin, z the facing, w the height range
layout(location = 3) in vec4 i_cues;  // x the pulse, y the sweep, z the x-ray, w the glow

layout(location = 0) out vec3 v_world;
layout(location = 1) flat out vec3 v_normal;
layout(location = 2) flat out vec4 v_feet;
layout(location = 3) flat out vec4 v_color;
layout(location = 4) flat out vec4 v_shape;
layout(location = 5) flat out vec4 v_cues;

void main() {
    // Six faces of two triangles: the face's axis and side, then the two other axes' corners.
    int face = gl_VertexIndex / 6, corner = gl_VertexIndex % 6;
    const vec2 quad[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
    vec2 q = quad[corner];
    float side = (face & 1) == 0 ? -1.0 : 1.0;
    int axis = face / 2;
    vec3 unit = axis == 0 ? vec3(side, q.x, q.y) : axis == 1 ? vec3(q.x, side, q.y) : vec3(q.x, q.y, side);
    vec3 normal = axis == 0 ? vec3(side, 0, 0) : axis == 1 ? vec3(0, side, 0) : vec3(0, 0, side);
    // Room for the sweep (from 1.6 radii in), the halo and the facing notch outside the band.
    float reach = i_feet.w * 1.7 + i_shape.x * 8.0;
    vec3 half_size = vec3(reach, reach, i_shape.w);
    vec3 p = i_feet.xyz + unit * half_size;
    v_world = p;
    v_normal = normal;
    v_feet = i_feet;
    v_color = i_color;
    v_shape = i_shape;
    v_cues = i_cues;
    gl_Position = pc.view_proj * vec4(p, 1.0);
}
