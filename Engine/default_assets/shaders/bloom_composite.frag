#version 410 core

in vec2 v_uv;
uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform float u_bloomStrength;

out vec4 fragColor;

void main() {
    vec3 hdr   = texture(u_scene, v_uv).rgb;
    vec3 bloom = texture(u_bloom, v_uv).rgb;
    // Output linear HDR with bloom added — PostProcessPass applies tonemapping
    fragColor = vec4(hdr + bloom * u_bloomStrength, 1.0);
}
