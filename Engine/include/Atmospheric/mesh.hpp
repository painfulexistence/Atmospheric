#pragma once
#include "buffer.hpp"
#include "bullet_collision.hpp"
#include "globals.hpp"
#include "material.hpp"
#include "shader.hpp"
#include "vertex.hpp"
#include <array>
#include <cstdint>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <optional>
#include <vector>

enum class MeshType {
    PRIM = 0,
    TERRAIN = 1,
    SKY = 2,
    DEBUG = 3,// Debug lines, wireframes
    CANVAS = 4,// UI/Canvas elements
    VOXEL = 5// Voxel chunk meshes
};

enum class UpdateFrequency {
    Static,// One-time upload, never changes (normal 3D models)
    Dynamic,// May change per frame (debug lines, canvas, particles)
    Stream// Always changes per frame
};

class Mesh {
public:
    MeshType type;
    UpdateFrequency updateFreq = UpdateFrequency::Static;
    size_t vertCount = 0;
    size_t triCount = 0;
    bool initialized = false;
    GLuint vao = 0;
    GLuint ibo = 0;

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

    MaterialHandle GetMaterial() const {
        return _material;
    }

    void SetMaterial(MaterialHandle material) {
        _material = material;
    };

    // Non-owning observer; the Mesh owns the shape.
    btCollisionShape* GetShape() const {
        return _shape.get();
    }

    // Takes ownership of a heap-allocated shape (callers pass `new btXxxShape(...)`).
    void SetShape(btCollisionShape* shape) {
        _shape.reset(shape);
    }

    void SetShapeLocalScaling(glm::vec3 localScaling);

    void AddCapsuleShape(float radius, float height);

private:
    GLuint vbo, ebo;
    GLenum _primitiveType = GL_TRIANGLES;
    std::array<glm::vec3, 8> _bounds;

    // Stable reference into AssetManager's material table; never dangles
    // (resolves to nullptr after the material is unloaded).
    MaterialHandle _material;
    std::unique_ptr<btCollisionShape> _shape;

    // New RenderMesh-based storage (used by Update methods)
    RenderMeshHandle _renderMeshHandle;

    template<typename VertexType> void InitializeDynamic(GLenum primType);
};
