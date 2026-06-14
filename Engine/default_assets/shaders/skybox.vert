#version 410 core

layout(location = 0) in vec3 aPos;

uniform mat4 u_proj;
uniform mat4 u_view;  // rotation-only (no translation)

out vec3 v_viewDir;

void main() {
    v_viewDir = aPos;
    // xyww forces depth = 1.0 (far plane), skybox renders behind everything
    vec4 pos = u_proj * u_view * vec4(aPos, 1.0);
    gl_Position = pos.xyww;
}
