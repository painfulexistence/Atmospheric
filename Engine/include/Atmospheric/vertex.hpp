#pragma once
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

struct DebugVertex {
    glm::vec3 position;
    glm::vec3 color;
};

struct ScreenVertex {
    glm::vec2 position;
    glm::vec2 texCoord;
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