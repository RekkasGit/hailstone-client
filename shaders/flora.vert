#version 450

// The radial flora (render/flora_renderer.h): a plant per instance, a quad standing on its foot turned by its turn, or
// two crossed ones (vertices 6..11; collapsed for a single quad). Its top sways to and fro over its period.

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 time; // x seconds
} pc;

layout(location = 0) in vec4 i_position_turn; // xyz the foot, w the turn (radians)
layout(location = 1) in vec4 i_size;          // x width, y height, z the sway's reach (of the height), w its period
layout(location = 2) in vec4 i_uv;            // the picture's corner and size in the texture
layout(location = 3) in vec4 i_shade;         // rgb the light, w 1 for two crossed quads
layout(location = 4) in vec4 i_haze;          // rgb the haze, w how much of the plant shows through
layout(location = 5) in uint i_texture;

layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out vec3 v_light;
layout(location = 2) flat out vec4 v_haze;
layout(location = 3) flat out uint v_texture;

void main() {
    const vec2 corners[6] = vec2[6](vec2(-1, 0), vec2(1, 0), vec2(1, 1), vec2(-1, 0), vec2(1, 1), vec2(-1, 1));
    int quad = gl_VertexIndex / 6;
    vec2 c = corners[gl_VertexIndex % 6];
    if (quad == 1 && i_shade.w < 0.5) c = vec2(0.0); // a single quad: the second collapses
    float turn = i_position_turn.w + float(quad) * 1.5707963;
    vec3 across = vec3(cos(turn), sin(turn), 0.0);
    vec3 foot = i_position_turn.xyz;
    vec3 p = foot + across * (c.x * i_size.x * 0.5) + vec3(0.0, 0.0, c.y * i_size.y);
    if (i_size.w > 0.0 && c.y > 0.5) {
        // The sway: the top moves along the plant's own line, its phase from where it stands.
        float phase = dot(foot.xy, vec2(0.13, 0.071));
        float s = sin(6.2831853 * pc.time.x / i_size.w + phase);
        vec3 way = vec3(cos(i_position_turn.w + 0.7), sin(i_position_turn.w + 0.7), 0.0);
        p += way * (s * i_size.z * i_size.y);
    }
    v_uv = i_uv.xy + vec2((c.x + 1.0) * 0.5, 1.0 - c.y) * i_uv.zw;
    v_light = i_shade.rgb;
    v_haze = i_haze;
    v_texture = i_texture;
    gl_Position = pc.view_proj * vec4(p, 1.0);
}
