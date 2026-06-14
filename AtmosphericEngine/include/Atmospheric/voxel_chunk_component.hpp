#pragma once
#include "component.hpp"
#include "mesh.hpp"
#include "mesh_builder.hpp"
#include "globals.hpp"
#include <array>
#include <cstdint>
#include <glm/vec3.hpp>

class GraphicsServer;

class VoxelChunkComponent : public Component {
public:
    static constexpr int SIZE = 32;

    VoxelChunkComponent(GameObject* owner, GraphicsServer* gfx, glm::ivec3 chunkPos);
    ~VoxelChunkComponent();

    std::string GetName() const override { return "VoxelChunkComponent"; }
    void OnAttach() override;
    void OnDetach() override;

    uint8_t GetVoxel(int x, int y, int z) const;
    void    SetVoxel(int x, int y, int z, uint8_t type);
    bool    IsAir(int x, int y, int z) const;
    bool    IsInBounds(int x, int y, int z) const;

    // dx, dz in {-1, 0, 1}
    void SetNeighbor(int dx, int dz, VoxelChunkComponent* neighbor);

    void RebuildMesh();

    bool       IsDirty()    const { return _dirty; }
    void       MarkDirty()        { _dirty = true; }
    Mesh*      GetMesh()    const { return _mesh; }
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
    GraphicsServer*      _gfx;
    glm::ivec3           _chunkPos;
    uint8_t              _voxels[SIZE][SIZE][SIZE];
    bool                 _dirty = true;
    Mesh*                _mesh  = nullptr;
    VoxelChunkComponent* _neighbors[3][3];

    uint8_t GetVoxelWithNeighbors(int x, int y, int z) const;
    void    BuildGreedyLayer(VoxelMeshBuilder& builder, int axis, int layer, int dir);
};
