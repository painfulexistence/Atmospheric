#version 410 core

uniform vec3  u_color;
uniform vec3  u_fogColor;
uniform float u_fogDensity;

out vec4 fragColor;

void main() {
    vec3 col  = u_color;
    float dist = gl_FragCoord.z / gl_FragCoord.w;
    col = mix(col, u_fogColor, 1.0 - exp(-u_fogDensity * dist * dist));
    fragColor = vec4(col, 1.0);
}
