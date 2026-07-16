#pragma once
#include "glm/mat4x4.hpp"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"

struct Vertex {
public:
    glm::vec3 position;
    glm::vec2 uv;
    glm::vec3 normal;
    glm::vec3 tangent;
    glm::vec3 bitangent;

    Vertex(
        const glm::vec3& pos = glm::vec3(0, 0, 0),
        const glm::vec2& uv = glm::vec2(0, 0),
        const glm::vec3& normal = glm::vec3(0, 0, 0),
        const glm::vec3& tangent = glm::vec3(0, 0, 0),
        const glm::vec3& bitangent = glm::vec3(0, 0, 0)
    ) {
        this->position = pos;
        this->uv = uv;
        this->normal = normal;
        this->tangent = tangent;
        this->bitangent = bitangent;
    }
};

// Per-vertex skinning attributes, kept in a PARALLEL buffer (not in Vertex) so
// static meshes pay nothing. joints index into the bound Skeleton::joints;
// weights are the blend weights (should sum to 1). From glTF JOINTS_0/WEIGHTS_0.
struct SkinVertex {
    glm::ivec4 joints{ 0, 0, 0, 0 };
    glm::vec4 weights{ 0.0f, 0.0f, 0.0f, 0.0f };
};

struct DebugVertex {
    glm::vec3 position;
    glm::vec3 color;
};

struct ScreenVertex {
    glm::vec2 position;
    glm::vec2 texCoord;
};

// One grass blade for instanced rendering (see TerrainStreamer's grass ring).
// A single canonical 9-vertex blade is drawn once per instance; everything
// that makes a blade unique lives here (32 bytes vs ~500 for baked geometry).
struct GrassInstance {
    glm::vec3 root;// blade root, cell-local
    float facing;// yaw angle (radians)
    float length;// blade length in metres
    float lean;// static forward lean (rest-pose bend)
    float phase;// wind flutter phase offset
    float hue;// [0,1] per-blade color variation
};

struct VoxelVertex {
    uint8_t x, y, z;// Local position within chunk (0-255)
    uint8_t voxel_id;// Voxel type
    uint8_t face_id;// Face direction (0-5: +Y, -Y, +X, -X, +Z, -Z)
    uint8_t ao;// Baked corner ambient occlusion, 0 (fully occluded) .. 3 (open)
    // Trailing padding: WebGPU requires GPUVertexBufferLayout.arrayStride to be
    // a multiple of 4 bytes, so the struct must round up to 8 bytes. (WebGPU
    // reads bytes 4..7 as one Uint8x4, so ao arrives as aFace.y in VOXEL_WGSL.)
    uint8_t _pad[2];
};

// Per-instance draw data streamed into the instanced-geometry attribute buffer
// (locations 5-8, divisor 1). One model matrix per instance; a batch uploads a
// contiguous array of these. Lives here (rather than graphics_subsystem.hpp) so
// RenderCommand and MeshInstancerComponent can name it without pulling in the subsystem.
struct InstanceData {
    glm::mat4 modelMatrix;
};