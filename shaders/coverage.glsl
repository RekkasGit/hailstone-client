// Alpha-to-coverage for the opaque pass (render/vk_context.h SceneOptions msaa): the pipeline sets constant_id 0
// when the scene is multisampled, and the alpha test then becomes the coverage of the pixel's samples.

layout(constant_id = 0) const bool kAlphaToCoverage = false;

// The alpha test's edge as coverage: a ramp about one pixel wide around the reference, so the edge is smooth under
// MSAA. The ramp is at most 0.05 of alpha wide: on far, minified textures the alpha changes a lot across a pixel, and
// a wider ramp gives texels well under the reference half coverage (dark specks against the sky in gfaydark).
float alpha_coverage(float alpha, float ref) {
    return clamp((alpha - ref) / clamp(fwidth(alpha), 1e-4, 0.05) + 0.5, 0.0, 1.0);
}

// The colour target's alpha after the alpha test (GREATEREQUAL against ref, 0 = off). Returns false to discard.
bool alpha_test(float alpha, float ref, float opacity, out float out_alpha) {
    if (kAlphaToCoverage) {
        out_alpha = ref > 0.0 ? alpha_coverage(alpha, ref) : 1.0;
        return true;
    }
    out_alpha = alpha * opacity;
    return !(ref > 0.0 && alpha < ref);
}
