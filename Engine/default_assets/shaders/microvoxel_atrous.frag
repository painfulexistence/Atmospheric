#version 410 core

// ============================================================================
// À-trous edge-stopping wavelet filter for the micro voxel GI (SVGF-lite)
// ============================================================================
// One iteration of a 5x5 B3-spline à-trous filter. The pass runs it a few
// times with increasing u_stepSize (1, 2, 4, ...) to denoise the 1-spp
// temporally-accumulated GI *spatially* without bleeding across geometry edges.
// Edge-stopping weights combine surface normal, camera distance (depth), and
// luminance. Radiance + camDist come from u_gi (ping-ponged between passes);
// the primary-hit normal comes from u_giNormal, which is constant across
// iterations (written once by the GI trace). This is what makes 1 spp look
// clean and kills the disocclusion / post-edit noise burst that pure temporal
// accumulation leaves behind.

#ifdef GL_ES
precision highp float;
precision highp sampler2D;
#endif

in vec2 v_uv;

uniform sampler2D u_gi;        // rgb = radiance, a = camera distance
uniform sampler2D u_giNormal;  // xyz = primary hit normal, w = validity
uniform vec2  u_texelSize;     // 1 / giResolution
uniform int   u_stepSize;      // à-trous hole size (1, 2, 4, ...)
uniform float u_sigmaDepth;    // depth edge-stopping falloff
uniform float u_sigmaNormal;   // normal edge-stopping exponent
uniform float u_sigmaLuma;     // luminance edge-stopping falloff

out vec4 fragColor;

float luma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec4 c = texture(u_gi, v_uv);
    vec3 n0 = texture(u_giNormal, v_uv).xyz;
    float z0 = c.a;
    // Miss / no surface: nothing to filter (the composite discards these pixels).
    if (z0 <= 0.0 || dot(n0, n0) < 1e-4) {
        fragColor = c;
        return;
    }
    float l0 = luma(c.rgb);

    // B3-spline row weights [1,4,6,4,1]/16.
    float kernel[5] = float[5](0.0625, 0.25, 0.375, 0.25, 0.0625);

    vec3 sum = vec3(0.0);
    float wSum = 0.0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            vec2 uv = v_uv + vec2(float(dx), float(dy)) * float(u_stepSize) * u_texelSize;
            vec4 ct = texture(u_gi, uv);
            float zt = ct.a;
            if (zt <= 0.0) continue;// skip miss taps
            vec3 nt = texture(u_giNormal, uv).xyz;
            float wN = pow(max(dot(n0, nt), 0.0), u_sigmaNormal);
            float wZ = exp(-abs(z0 - zt) / (u_sigmaDepth * float(u_stepSize) + 1e-3));
            float wL = exp(-abs(l0 - luma(ct.rgb)) / (u_sigmaLuma + 1e-3));
            float w = kernel[dx + 2] * kernel[dy + 2] * wN * wZ * wL;
            sum += w * ct.rgb;
            wSum += w;
        }
    }

    vec3 outRgb = (wSum > 1e-5) ? sum / wSum : c.rgb;
    fragColor = vec4(outRgb, z0);// keep camDist for the next iteration's depth weight
}
