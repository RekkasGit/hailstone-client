#version 450

// The modern lighting's spell bolts and comets (render/spell_fx_renderer.h): one piece per instance, built here.
//   orb, burst (kinds 0, 3): a quad facing the camera round a point;
//   trail, spark, beam (1, 2, 5): a quad along a segment from its tail (b) to its head (a), turned about it to face
//     the camera, reaching past both ends by their radii (rounded in the fragment shader);
//   ring (4): a level quad round a point (a shockwave over the ground).

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 camera_time; // xyz the camera, w seconds
    vec4 screen;
    vec4 haze;
} pc;

layout(location = 0) in vec4 i_a;      // xyz the point (a segment's head), w its radius
layout(location = 1) in vec4 i_b;      // xyz a segment's tail, w its radius there
layout(location = 2) in vec4 i_kind;   // x the kind, y the element, z the age (0..1), w the seed
layout(location = 3) in vec4 i_extra;  // x the intensity, y the phase (orb: 0 flying, 1 waiting, 2 fizzling)

layout(location = 0) out vec2 v_local;       // orb, burst, ring: -1..1; segments: x along from the tail (world), y across
layout(location = 1) out vec3 v_world;
layout(location = 2) flat out vec4 v_kind;
layout(location = 3) flat out vec4 v_extra;
layout(location = 4) flat out vec4 v_segment; // x its length, y the tail's radius, z the head's, w the beam's swing

void main() {
    const vec2 corners[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
    vec2 c = corners[gl_VertexIndex];
    int kind = int(i_kind.x + 0.5);
    vec3 p;
    v_segment = vec4(0.0);
    if (kind == 0 || kind == 3) {
        vec3 forward = normalize(i_a.xyz - pc.camera_time.xyz);
        vec3 right = normalize(cross(forward, vec3(0.0, 0.0, 1.0)) + vec3(1e-4, 0.0, 0.0));
        vec3 up = cross(right, forward);
        p = i_a.xyz + (right * c.x + up * c.y) * i_a.w - forward * i_a.w * 0.5;
        v_local = c;
    } else if (kind == 4) {
        p = i_a.xyz + vec3(c.x, c.y, 0.0) * i_a.w + vec3(0.0, 0.0, 0.3);
        v_local = c;
    } else {
        vec3 head = i_a.xyz, tail = i_b.xyz;
        vec3 axis = head - tail;
        float len = length(axis);
        vec3 dir = len > 1e-4 ? axis / len : vec3(0.0, 0.0, 1.0);
        vec3 mid = (head + tail) * 0.5;
        vec3 side = cross(dir, mid - pc.camera_time.xyz);
        float sl = length(side);
        side = sl > 1e-4 ? side / sl : vec3(1.0, 0.0, 0.0);
        float swing = kind == 5 ? min(len * 0.09, 3.5) : 0.0; // the lightning's zigzag needs room
        float t = c.x * 0.5 + 0.5;
        float r = mix(i_b.w, i_a.w, t);
        float along = mix(-i_b.w, len + i_a.w, t);
        float across = c.y * (r + swing);
        p = tail + dir * along + side * across;
        v_local = vec2(along, across);
        v_segment = vec4(len, i_b.w, i_a.w, swing);
    }
    v_world = p;
    v_kind = i_kind;
    v_extra = i_extra;
    gl_Position = pc.view_proj * vec4(p, 1.0);
}
