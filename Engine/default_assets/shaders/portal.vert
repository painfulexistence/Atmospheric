#version 410 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aNormal;

uniform mat4 u_model;
uniform mat4 u_viewProj;

out vec3 v_worldPos;
out vec2 v_uv;
out vec3 v_normal;
out vec4 v_clipPos;

void main() {
    v_worldPos = vec3(u_model * vec4(aPos, 1.0));
    v_normal   = normalize(mat3(u_model) * aNormal);
    v_uv       = aUV;

    gl_Position = u_viewProj * vec4(v_worldPos, 1.0);
    v_clipPos   = gl_Position;
}
