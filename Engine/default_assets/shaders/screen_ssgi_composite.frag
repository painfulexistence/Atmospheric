#version 410 core

// SSGI composite: depth-aware (bilateral) blur of the noisy one-bounce SSGI
// buffer, then add it to the scene color. The blur is edge-stopped by depth so
// indirect light doesn't bleed across silhouettes. Runs over the resolved scene.

in vec2 v_uv;

uniform sampler2D u_color;
uniform sampler2D u_ssgi;
uniform sampler2D u_depth;
uniform vec2  u_viewportSize;
uniform float u_ssgiStrength;

out vec4 fragColor;

void main() {
    vec3 col = texture(u_color, v_uv).rgb;
    float d = texture(u_depth, v_uv).r;
    if (d >= 1.0) {
        fragColor = vec4(col, 1.0);
        return;
    }

    vec2 texel = 1.0 / u_viewportSize;
    vec3 sum = vec3(0.0);
    float wsum = 0.0;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            vec2 o = vec2(float(dx), float(dy)) * texel;
            float sd = texture(u_depth, v_uv + o).r;
            float w = exp(-abs(sd - d) * 4000.0);// depth edge-stop
            sum += texture(u_ssgi, v_uv + o).rgb * w;
            wsum += w;
        }
    }
    vec3 ssgi = sum / max(wsum, 1e-4);
    fragColor = vec4(col + u_ssgiStrength * ssgi, 1.0);
}
