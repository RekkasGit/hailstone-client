// Dimensionless AO kernel. Also compiled by the CPU regression tests with GLM's float operations.
#ifndef HAILSTONE_AO_KERNEL_GLSL
#define HAILSTONE_AO_KERNEL_GLSL

float ao_kernel_radius(float radius, float pixel_radius, float view_depth, float tan_half_y, float height) {
    return min(radius, pixel_radius * 2.0f * view_depth * tan_half_y / height);
}

float ao_sample(vec3 offset, vec3 normal, float radius, float bias) {
    float distance2 = dot(offset, offset);
    float falloff = max(1.0f - distance2 / (radius * radius), 0.0f);
    // Ignore almost coplanar samples. Unlike n.v / |v|^2, this angular term cannot grow as samples get closer.
    float cosine = (dot(offset, normal) - bias) * inversesqrt(max(distance2, 1e-8f));
    float horizon = clamp((cosine - 0.1f) / 0.9f, 0.0f, 1.0f);
    return falloff * falloff * falloff * horizon;
}

float ao_visibility(float sum, float intensity, float samples) {
    return clamp(1.0f - sum * intensity / samples, 0.0f, 1.0f);
}

#endif
