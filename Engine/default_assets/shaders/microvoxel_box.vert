#version 410 core

// Bounding-box vertex pass for per-object micro voxel raymarching.
// ============================================================================
// The shared unit cube ([-1,1]^3, the skybox VAO) is transformed by u_model to
// the volume's world-space AABB, so only the pixels the box covers run the
// (expensive) DDA in microvoxel.frag — instead of the whole screen. This is
// what makes many small volumes affordable. The fragment reconstructs its view
// ray from the interpolated world position (v_worldPos), not from a fullscreen
// inverse-projection.
//
// Draw the cube with culling disabled so the box still covers its footprint
// when the camera is inside it; the fragment writes gl_FragDepth from the DDA
// hit, so volumes depth-composite with each other and the rasterized scene.
layout(location = 0) in vec3 position;

uniform mat4 u_model;    // unit cube -> world AABB (translate * scale)
uniform mat4 u_viewProj;

out vec3 v_worldPos;

void main() {
    vec4 world = u_model * vec4(position, 1.0);
    v_worldPos = world.xyz;
    gl_Position = u_viewProj * world;
}
