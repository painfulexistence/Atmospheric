#version 410 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;

out vec2 v_uv;

void main() {
    v_uv        = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
