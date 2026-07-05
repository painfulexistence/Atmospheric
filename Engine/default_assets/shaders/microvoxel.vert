#version 410 core

// Fullscreen pass over the shared screen-quad VAO (same layout as hdr.vert).
// Pairs with microvoxel.frag (raymarched micro voxels).
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;

out vec2 v_uv;

void main() {
    v_uv = uv;// 0..1 across the screen
    gl_Position = vec4(position, 1.0);
}
