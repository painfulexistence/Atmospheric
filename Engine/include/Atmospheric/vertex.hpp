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
    // Trailing padding: WebGPU requires GPUVertexBufferLayout.arrayStride to be
    // a multiple of 4 bytes, so the struct must round up from 5 to 8 bytes.
    uint8_t _pad[3];
};