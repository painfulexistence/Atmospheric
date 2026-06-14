#version 410 core

in vec2 v_uv;
uniform sampler2D u_scene;
uniform float u_threshold;

out vec4 fragColor;

void main() {
    vec3 color      = texture(u_scene, v_uv).rgb;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    fragColor = (brightness > u_threshold) ? vec4(color, 1.0) : vec4(0.0);
}
