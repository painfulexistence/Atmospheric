#pragma once
#include "mesh.hpp"
#include "mesh_builder.hpp"
#include "globals.hpp"
#include <array>
#include <cstdint>
#include <glm/vec3.hpp>

class GraphicsServer;

class VoxelChunk {
public:
    static constexpr int SIZE = 32;

    explicit VoxelChunk(glm::ivec3 chunkPos);
    ~VoxelChunk();

    uint8_t GetVoxel(int x, int y, int z) const;
    void    SetVoxel(int x, int y, int z, uint8_t type);
    bool    IsAir(int x, int y, int z) const;
    bool    IsInBounds(int x, int y, int z) const;

    // Weak reference to an adjacent chunk for cross-boundary face culling.
    // dx, dz in {-1, 0, 1}.
    void SetNeighbor(int dx, int dz, VoxelChunk* chunk);

    // Rebuild GPU mesh using greedy meshing.  Clears the dirty flag.
    void RebuildMesh(GraphicsServer* gfx);

    bool        IsDirty()   const { return _dirty; }
    void        MarkDirty()       { _dirty = true;  }
    Mesh*       GetMesh()   const { return _mesh;   }
    glm::ivec3  GetChunkPos() const { return _chunkPos; }
    glm::vec3   GetWorldPos()  const {
        return glm::vec3(_chunkPos) * static_cast<float>(SIZE);
    }

    glm::vec3 GetBoundingSphereCenter() const;
    float     GetBoundingSphereRadius() const;

private:
    glm::ivec3 _chunkPos;
    uint8_t    _voxels[SIZE][SIZE][SIZE];
    bool       _dirty = true;
    Mesh*      _mesh  = nullptr;

    // [dx+1][dz+1],  dx/dz in {-1,0,1}
    VoxelChunk* _neighbors[3][3];

    uint8_t GetVoxelWithNeighbors(int x, int y, int z) const;

    // Build one layer of greedy quads perpendicular to `axis` (0=X,1=Y,2=Z).
    // `dir`: +1 = positive face, -1 = negative face.
    void BuildGreedyLayer(VoxelMeshBuilder& builder,
                          int axis, int layer, int dir);

    // sqrt(3)/2 * SIZE  (half-diagonal of the chunk AABB)
    static constexpr float BSPHERE_RADIUS =
        static_cast<float>(SIZE) * 0.5f * 1.7320508f;
};
