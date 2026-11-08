#version 450

// Dear ImGui's own vertex shader (backends/imgui_impl_vulkan.cpp), for the name plates' pipeline
// (app/nameplate_layer.h): the same vertices and push constants, so ImGui's draw commands run through it.

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(push_constant) uniform Push { vec2 scale; vec2 translate; } pc;

layout(location = 0) out struct { vec4 color; vec2 uv; } outv;

void main() {
    outv.color = aColor;
    outv.uv = aUV;
    gl_Position = vec4(aPos * pc.scale + pc.translate, 0.0, 1.0);
}
