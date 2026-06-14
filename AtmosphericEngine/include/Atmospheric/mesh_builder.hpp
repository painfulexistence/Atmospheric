#pragma once
#include "csg.hpp"
#include "buffer.hpp"
#include "vertex.hpp"


class Mesh;

// Face direction enum for voxel meshes
enum class FaceDir : uint8_t {
    TOP    = 0,  // +Y
    BOTTOM = 1,  // -Y
    RIGHT  = 2,  // +X
    LEFT   = 3,  // -X
    FRONT  = 4,  // +Z
    BACK   = 5   // -Z
};

class MeshBuilder {
public:
    static Mesh* CreateCube(const float& size = 1.0f);
    static Mesh* CreatePlane(float width, float height);

    static Mesh* CreateSphere(const float& radius = 0.5f, const int& division = 18);

    static Mesh* CreateTerrain(const float& size = 1024.f, const int& resolution = 10);

    static Mesh* CreateCubeWithPhysics(const float& size = 1.0f);

    static Mesh* CreateSphereWithPhysics(const float& radius = 0.5f, const int& division = 18);

    static Mesh* CreateTerrainWithPhysics(
      const float& size = 1024.f,
      const int& resolution = 10,
      const std::string& heightmap = "assets/textures/heightmap_debug.jpg"
    );

    void PushQuad(
      glm::vec3 position,
      glm::vec2 size,
      glm::vec3 normal,
      glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
      glm::vec2 uvMin = glm::vec2(0.0f),
      glm::vec2 uvMax = glm::vec2(1.0f)
    );

    void PushCube(glm::vec3 position, glm::vec3 size, glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

    // Push CSG compiled boxes
    void PushCSG(const std::vector<CSG::AABB>& boxes);

    std::shared_ptr<Mesh> Build();

    void Clear();

private:
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
};

// Builder for voxel chunk meshes using compact VoxelVertex format
class VoxelMeshBuilder {
public:
    // Push a single voxel face
    // pos: local position within chunk (0-255 range)
    // dir: face direction
    // voxelId: voxel type identifier
    void PushFace(glm::ivec3 pos, FaceDir dir, uint8_t voxelId);

    // Push a full cube (all 6 faces) - use when all faces are visible
    void PushCube(glm::ivec3 pos, uint8_t voxelId);

    // Push a greedy-merged quad.
    // pos      : base corner in local chunk space (quadPos[axis]=layer,
    //            quadPos[u_axis]=u_start, quadPos[v_axis]=v_start)
    // dir      : face direction
    // voxelId  : voxel type
    // w        : extent along u_axis
    // h        : extent along v_axis
    // u_axis   : 0=X, 1=Y, 2=Z
    // v_axis   : 0=X, 1=Y, 2=Z  (u_axis != v_axis, both != normal axis)
    void PushGreedyFace(glm::ivec3 pos, FaceDir dir, uint8_t voxelId,
                        int w, int h, int u_axis, int v_axis);

    // Build and return vertex data
    const std::vector<VoxelVertex>& Build() {
        return _vertices;
    }

    // Get vertex count
    size_t GetVertexCount() const {
        return _vertices.size();
    }

    // Clear accumulated data
    void Clear();

private:
    std::vector<VoxelVertex> _vertices;
};
