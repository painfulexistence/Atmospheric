#version 410 core

// Fullscreen triangle — no vertex buffer needed; positions are generated from
// gl_VertexID. Pairs with microvoxel.frag (raymarched micro voxels).
out vec2 v_uv;

void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_uv = p;                       // 0..2 range; frag reconstructs ray from NDC
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
