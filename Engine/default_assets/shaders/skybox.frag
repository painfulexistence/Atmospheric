#version 410 core

in vec3 v_viewDir;

uniform vec3 u_skyColor;
uniform vec3 u_horizonColor;

out vec4 fragColor;

void main() {
    vec3 dir = normalize(v_viewDir);
    float t  = clamp((dir.y + 1.0) * 0.5, 0.0, 1.0);
    vec3 col = mix(u_horizonColor, u_skyColor, t);
    fragColor = vec4(col, 1.0);
}
