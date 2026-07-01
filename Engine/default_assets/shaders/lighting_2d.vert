#version 410

// GL 4.1 vertex shader for LightingSystem2D fullscreen post-process.
// Companion fragment shader: lighting_2d.frag
// WebGL2 / Emscripten builds auto-translate this to "#version 300 es" —
// see PreprocessShaderForWebGL() in shader.cpp.

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;

out vec2 v_uv;

void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv = a_uv;
}
