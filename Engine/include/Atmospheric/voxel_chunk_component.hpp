#pragma once
#include "component.hpp"
#include "mesh.hpp"
#include "mesh_builder.hpp"
#include "globals.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <glm/vec3.hpp>

class GraphicsSubsystem;

class VoxelChunkComponent : public Component {
public:
    static constexpr int SIZE = 32;

    VoxelChunkComponent(GameObject* owner, GraphicsSubsystem* gfx, glm::ivec3 chunkPos);
    ~VoxelChunkComponent();

    std::string GetName() const override { return "VoxelChunk"; }
    void OnAttach() override;
    void OnDetach() override;

    uint8_t GetVoxel(int x, int y, int z) const;
    void    SetVoxel(int x, int y, int z, uint8_t type);
    bool    IsAir(int x, int y, int z) const;
    bool    IsInBounds(int x, int y, int z) const;

    // dx, dy, dz in {-1, 0, 1}
    void SetNeighbor(int dx, int dy, int dz, VoxelChunkComponent* neighbor);

    void RebuildMesh();
    std::vector<VoxelVertex> GenerateMeshData();
    void UploadMesh(const std::vector<VoxelVertex>& verts);

    // Re-assign this slot to a new world chunk coordinate (used by chunk streaming).
    // Clears voxels, detaches all neighbors, and uploads an empty mesh immediately.
    void Relocate(glm::ivec3 newChunkPos);

    bool       IsDirty()    const { return _dirty; }
    void       MarkDirty()        { _dirty = true; }
    Mesh*      GetMesh()    const { return _mesh.get(); }
    MeshHandle GetMeshHandle() const { return _meshHandle; }
    glm::ivec3 GetChunkPos() const { return _chunkPos; }
    glm::vec3  GetWorldPos()  const {
        return glm::vec3(_chunkPos) * static_cast<float>(SIZE);
    }

    glm::vec3 GetBoundingSphereCenter() const;
    float     GetBoundingSphereRadius() const;

    // Half-diagonal of the chunk cube — used for sphere frustum culling
    static constexpr float BSPHERE_RADIUS =
        static_cast<float>(SIZE) * 0.5f * 1.7320508f;

private:
    GraphicsSubsystem*      _gfx;
    glm::ivec3           _chunkPos;
    uint8_t              _voxels[SIZE][SIZE][SIZE];
    bool                 _dirty = true;
    std::unique_ptr<Mesh> _mesh;
    MeshHandle           _meshHandle;
    // _neighbors[dx+1][dy+1][dz+1], dx/dy/dz in {-1,0,1}
    VoxelChunkComponent* _neighbors[3][3][3];

    uint8_t GetVoxelWithNeighbors(int x, int y, int z) const;
    void    BuildGreedyLayer(VoxelMeshBuilder& builder, int axis, int layer, int dir);
};
