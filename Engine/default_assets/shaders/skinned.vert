#version 410

// ─────────────────────────────────────────────────────────────────────────────
// GPU skeletal skinning (linear blend skinning).
//
// Each vertex carries up to 4 joint indices + weights (locations 9/10, a
// parallel skin buffer). The joint matrix palette — one skinning matrix per
// joint, model-space × inverse-bind, recomputed each frame by SkeletalComponent
// — is uploaded as an RGBA32F "bone_map": 4 texels per joint (the mat4's four
// columns), addressed by texelFetch. localPos/normal are transformed by the
// weighted blend of the four joint matrices, then handed to the same World/TBN
// path as the static ("color") and VAT shaders so pbr.frag lights it unchanged.
// ─────────────────────────────────────────────────────────────────────────────

uniform mat4 ProjectionView;

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec3 tangent;
layout(location = 4) in vec3 bitangent;
layout(location = 5) in mat4 World;
layout(location = 9) in ivec4 joints; // indices into the bone palette
layout(location = 10) in vec4 weights;// blend weights (sum ~1)

uniform sampler2D bone_map;// RGBA32F, width = joint_count*4 (4 columns per joint), height 1
uniform int joint_count;
uniform int skin_enabled;// 0 = fall back to static attributes

out vec3 frag_pos;
out vec2 tex_uv;
out mat3 TBN;

mat4 boneMatrix(int j) {
    int x = j * 4;
    return mat4(
        texelFetch(bone_map, ivec2(x + 0, 0), 0),
        texelFetch(bone_map, ivec2(x + 1, 0), 0),
        texelFetch(bone_map, ivec2(x + 2, 0), 0),
        texelFetch(bone_map, ivec2(x + 3, 0), 0)
    );
}

void main() {
    vec3 localPos = position;
    vec3 localNorm = normal;
    vec3 localTan = tangent;

    if (skin_enabled == 1 && joint_count > 0) {
        // Clamp indices so a malformed asset can't fetch out of the palette.
        ivec4 j = clamp(joints, ivec4(0), ivec4(joint_count - 1));
        mat4 skin = weights.x * boneMatrix(j.x) + weights.y * boneMatrix(j.y) + weights.z * boneMatrix(j.z)
                    + weights.w * boneMatrix(j.w);
        // A vertex with no weights (all zero) would collapse to the origin;
        // fall back to identity in that case.
        if (weights.x + weights.y + weights.z + weights.w > 1e-4) {
            localPos = (skin * vec4(position, 1.0)).xyz;
            mat3 skin3 = mat3(skin);
            localNorm = skin3 * normal;
            localTan = skin3 * tangent;
        }
    }

    frag_pos = vec3(World * vec4(localPos, 1.0));
    tex_uv = uv;

    vec3 N = normalize(vec3(World * vec4(localNorm, 0.0)));
    vec3 T;
    if (dot(localTan, localTan) > 1e-6) {
        T = normalize(vec3(World * vec4(localTan, 0.0)));
        T = normalize(T - dot(T, N) * N);
    } else {
        vec3 up = abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        T = normalize(cross(up, N));
    }
    vec3 B = cross(N, T);
    TBN = mat3(T, B, N);

    gl_Position = ProjectionView * vec4(frag_pos, 1.0);
}

// NOTES: Uniform location qualifiers only available in version 4.3 or after
