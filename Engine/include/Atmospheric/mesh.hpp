#pragma once
#include "bullet_collision.hpp"
#include "globals.hpp"
#include "material.hpp"
#include "buffer.hpp"
#include "shader.hpp"
#include "vertex.hpp"
#include <array>
#include <cstdint>
#include <glm/gtc/quaternion.hpp>
#include <vector>

enum class MeshType {
    PRIM    = 0,
    TERRAIN = 1,
    SKY     = 2,
    DEBUG   = 3,  // Debug lines, wireframes
    CANVAS  = 4,  // UI/Canvas elements
    VOXEL   = 5   // Voxel chunk meshes
};

enum class UpdateFrequency {
    Static,   // One-time upload, never changes (normal 3D models)
    Dynamic,  // May change per frame (debug lines, canvas, particles)
    Stream    // Always changes per frame
};

class Mesh {
public:
    MeshType type;
    UpdateFrequency updateFreq = UpdateFrequency::Static;
    size_t vertCount;
    size_t triCount;
    bool initialized = false;
    GLuint vao;
    GLuint ibo;

    Mesh(MeshType type = MeshType::PRIM);
    ~Mesh();

    void Initialize(const std::vector<Vertex>& verts);
    void Initialize(const std::vector<Vertex>& verts, const std::vector<uint16_t>& tris);

    // Dynamic update methods for per-frame geometry (legacy)
    template<typename VertexType>
    void UpdateDynamic(const std::vector<VertexType>& verts, GLenum primType = GL_TRIANGLES);

    // Update with voxel vertex data (uses internal RenderMesh)
    void Update(const std::vector<VoxelVertex>& vertices);

    // Check if this mesh uses the new RenderMesh system
    bool UsesRenderMesh() const {
        return _renderMeshHandle.IsValid();
    }

    // Get the RenderMesh handle (for rendering)
    RenderMeshHandle GetRenderMeshHandle() const {
        return _renderMeshHandle;
    }

    GLenum GetPrimitiveType() const {
        return _primitiveType;
    }

    std::array<glm::vec3, 8> GetBoundingBox() const {
        return _bounds;
    }

    void SetBoundingBox(const std::array<glm::vec3, 8> bounds) {
        _bounds = bounds;
    }

    Material* GetMaterial() const {
        return _material;
    }

    void SetMaterial(Material* material) {
        _material = material;
    };

    btCollisionShape* GetShape() {
        return _shape;
    }

    void SetShape(btCollisionShape* shape) {
        _shape = shape;
    }

    void SetShapeLocalScaling(glm::vec3 localScaling);

    void AddCapsuleShape(float radius, float height);

private:
    GLuint vbo, ebo;
    GLenum _primitiveType = GL_TRIANGLES;
    std::array<glm::vec3, 8> _bounds;

    Material* _material;
    btCollisionShape* _shape;

    // New RenderMesh-based storage (used by Update methods)
    RenderMeshHandle _renderMeshHandle;

    template<typename VertexType> void InitializeDynamic(GLenum primType);
};
