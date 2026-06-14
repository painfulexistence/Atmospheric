#version 410 core

layout(location = 0) in vec3 aPos;

uniform mat4 u_model;
uniform mat4 u_viewProj;

void main() {
    gl_Position = u_viewProj * u_model * vec4(aPos, 1.0);
}
