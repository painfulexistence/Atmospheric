#version 410 core

// à-trous edge-stopping wavelet filter for SSGI (the spatial half of SVGF-lite).
// Runs a few iterations with a growing step size (1, 2, 4, …), each a 5x5
// B3-spline kernel. Edge-stopping uses tangent-plane distance (center normal
// reconstructed from depth, so no normal buffer) plus luma, so indirect light
// doesn't bleed across silhouettes or wash out detail. Display-only — never fed
// back into the temporal history.

in vec2 v_uv;

uniform sampler2D u_ssgi;
uniform sampler2D u_depth;
uniform mat4  u_invProj;
uniform vec2  u_viewportSize;
uniform float u_stepSize;   // 1, 2, 4, …
uniform float u_sigmaDepth; // tangent-plane distance sigma (metres)
uniform float u_sigmaLuma;

out vec4 fragColor;

const float K[5] = float[](1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0);

vec3 viewPos(vec2 uv, float d) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 v = u_invProj * ndc;
    return v.xyz / v.w;
}

float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    float dc = texture(u_depth, v_uv).r;
    vec4 center = texture(u_ssgi, v_uv);
    if (dc >= 1.0) {
        fragColor = center;
        return;
    }
    vec3 Pc = viewPos(v_uv, dc);
    vec3 Nc = normalize(cross(dFdx(Pc), dFdy(Pc)));
    float lc = luma(center.rgb);
    vec2 texel = 1.0 / u_viewportSize;

    vec3 sum = vec3(0.0);
    float wsum = 0.0;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            vec2 uv = v_uv + vec2(float(dx), float(dy)) * u_stepSize * texel;
            float d = texture(u_depth, uv).r;
            if (d >= 1.0) continue;
            vec3 s = texture(u_ssgi, uv).rgb;
            vec3 Pt = viewPos(uv, d);
            float wPlane = exp(-abs(dot(Nc, Pt - Pc)) / max(u_sigmaDepth, 1e-3));
            float wLuma = exp(-abs(luma(s) - lc) / max(u_sigmaLuma, 1e-3));
            float w = K[dx + 2] * K[dy + 2] * wPlane * wLuma;
            sum += s * w;
            wsum += w;
        }
    }
    fragColor = vec4(wsum > 0.0 ? sum / wsum : center.rgb, center.a);
}
