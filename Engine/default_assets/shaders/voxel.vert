#version 410 core

layout(location = 0) in uvec3 aPos;
layout(location = 1) in uint aVoxelId;
layout(location = 2) in uint aFaceId;
layout(location = 3) in uint aAO;// baked corner AO, 0 (occluded) .. 3 (open)

uniform mat4 u_model;
uniform mat4 u_viewProj;

out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_uv;
flat out uint v_voxelId;
flat out uint v_faceId;
// Not flat: AO is per-vertex and interpolates across the (greedy) quad.
out float v_ao;

// Face normals indexed by FaceDir: +Y, -Y, +X, -X, +Z, -Z
const vec3 FACE_NORMALS[6] = vec3[](
    vec3( 0.0,  1.0,  0.0),
    vec3( 0.0, -1.0,  0.0),
    vec3( 1.0,  0.0,  0.0),
    vec3(-1.0,  0.0,  0.0),
    vec3( 0.0,  0.0,  1.0),
    vec3( 0.0,  0.0, -1.0)
);

void main() {
    vec3 localPos = vec3(aPos);
    v_worldPos = vec3(u_model * vec4(localPos, 1.0));
    v_normal   = mat3(transpose(inverse(u_model))) * FACE_NORMALS[aFaceId];
    v_voxelId  = aVoxelId;
    v_faceId   = aFaceId;
    v_ao       = float(aAO) / 3.0;// 0..1

    // UV derived from local position so greedy quads tile naturally.
    // fract() in the fragment shader produces per-voxel tiling.
    if (aFaceId == 0u || aFaceId == 1u) {
        v_uv = vec2(localPos.x, localPos.z); // TOP / BOTTOM
    } else if (aFaceId == 2u || aFaceId == 3u) {
        v_uv = vec2(localPos.z, localPos.y); // RIGHT / LEFT
    } else {
        v_uv = vec2(localPos.x, localPos.y); // FRONT / BACK
    }

    gl_Position = u_viewProj * vec4(v_worldPos, 1.0);
}
