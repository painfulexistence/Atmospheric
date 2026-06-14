#version 410 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aNormal;

uniform mat4  u_model;
uniform mat4  u_viewProj;
uniform float u_time;
uniform float u_waveStrength;
uniform float u_waveSpeed;

out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_uv;

void main() {
    vec3 pos = aPos;
    float wave = sin(u_time * u_waveSpeed + aPos.x * 0.5)
               * cos(u_time * u_waveSpeed + aPos.z * 0.5)
               * u_waveStrength;
    pos.y += wave;

    v_worldPos = vec3(u_model * vec4(pos, 1.0));
    v_normal   = normalize(mat3(transpose(inverse(u_model))) * aNormal);
    v_uv       = aUV;

    gl_Position = u_viewProj * vec4(v_worldPos, 1.0);
}
