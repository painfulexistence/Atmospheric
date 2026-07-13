#version 410 core

// SSGI composite: add the (temporally accumulated, à-trous-filtered) indirect
// to the scene color. Spatial denoising now lives in the à-trous pass, so this
// is a straight add.

in vec2 v_uv;

uniform sampler2D u_color;
uniform sampler2D u_ssgi;
uniform sampler2D u_depth;
uniform float u_ssgiStrength;

out vec4 fragColor;

void main() {
    vec3 col = texture(u_color, v_uv).rgb;
    float d = texture(u_depth, v_uv).r;
    if (d >= 1.0) {
        fragColor = vec4(col, 1.0);
        return;
    }
    vec3 ssgi = texture(u_ssgi, v_uv).rgb;
    fragColor = vec4(col + u_ssgiStrength * ssgi, 1.0);
}
