#pragma once
#include "voxel_chunk_component.hpp"
#include "frustum.hpp"
#include "renderer.hpp"
#include <glm/vec3.hpp>
#include <vector>

class Application;
class GraphicsServer;

class VoxelWorld {
public:
    static constexpr int   WORLD_X    = 25;
    static constexpr int   WORLD_Y    = 3;
    static constexpr int   WORLD_Z    = 25;
    static constexpr float WATER_LINE = 32.0f;

    VoxelWorld() = default;
    ~VoxelWorld() = default;

    // Creates GameObjects with VoxelChunkComponents via the Application.
    void Init(Application* app, int seed = 42);
    void Update(float dt, const glm::vec3& cameraPos);

    // Submit visible chunk render commands to the renderer opaque queue.
    void SubmitRenderCommands(Renderer* renderer, const glm::mat4& viewProj,
                              const glm::vec3& cameraPos);

    uint8_t GetVoxel(int wx, int wy, int wz) const;
    void    SetVoxel(int wx, int wy, int wz, uint8_t type);

private:
    std::vector<VoxelChunkComponent*> _chunks;
    GraphicsServer* _gfx  = nullptr;
    int             _seed = 42;

    VoxelChunkComponent* GetChunk(int cx, int cy, int cz) const;
    int                  ChunkIndex(int cx, int cy, int cz) const;

    void GenerateTerrain();
    void LinkNeighbors();
    void RebuildDirtyChunks();
};
