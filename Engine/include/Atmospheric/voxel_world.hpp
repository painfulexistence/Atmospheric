#pragma once
#include "voxel_chunk_component.hpp"
#include "frustum.hpp"
#include "renderer.hpp"
#include <glm/vec3.hpp>
#include <unordered_map>
#include <vector>

class Application;
class GraphicsSubsystem;
class GameObject;

struct IVec3Hash {
    size_t operator()(const glm::ivec3& v) const noexcept {
        size_t h = std::hash<int>{}(v.x);
        h ^= std::hash<int>{}(v.y) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(v.z) + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};

class VoxelWorld {
public:
    // Y extent is bounded (no underground streaming needed).
    static constexpr int   WORLD_Y    = 3;
    // View radius in chunks for X and Z — chunks within this range are kept loaded.
    // 12 gives a 25x25 total footprint (2*12+1), matching VX's WORLD_WIDTH/WORLD_DEPTH.
    static constexpr int   VIEW_X     = 12;
    static constexpr int   VIEW_Z     = 12;

    VoxelWorld() = default;
    ~VoxelWorld() = default;

    // root: all game objects created by VoxelWorld (chunks, sun) are parented
    // here so they don't pollute the top-level entity list.
    void Init(Application* app, int seed = 42, GameObject* root = nullptr);
    void Update(float dt, const glm::vec3& cameraPos);

    // When true, chunks stream in/out as the camera moves.
    // When false (default), only the initial view volume loaded in Init() is kept.
    bool infiniteMode = false;

    void SubmitRenderCommands(Renderer* renderer, const glm::mat4& viewProj,
                              const glm::vec3& cameraPos);

    uint8_t GetVoxel(int wx, int wy, int wz) const;
    void    SetVoxel(int wx, int wy, int wz, uint8_t type);

private:
    using ChunkMap = std::unordered_map<glm::ivec3, VoxelChunkComponent*, IVec3Hash>;

    // Max new chunks meshed per Update() to avoid per-frame stutter.
    static constexpr int LOAD_PER_FRAME = 8;
    // Unload margin beyond VIEW_X/Z to prevent thrashing at chunk boundaries.
    static constexpr int UNLOAD_MARGIN  = 2;

    ChunkMap                          _chunkMap;
    std::vector<VoxelChunkComponent*> _pool;      // recycled unloaded slots
    Application*                      _app       = nullptr;
    GraphicsSubsystem*                   _gfx       = nullptr;
    int                               _seed      = 42;
    glm::ivec3                        _lastCamChunk{INT_MIN, 0, INT_MIN};
    GameObject*                       _root      = nullptr;

    VoxelChunkComponent* GetChunk(int cx, int cy, int cz) const;
    VoxelChunkComponent* AcquireSlot(glm::ivec3 pos);

    void LoadChunk(glm::ivec3 pos);
    void UnloadChunk(glm::ivec3 pos);
    void GenerateChunkTerrain(VoxelChunkComponent* chunk);
    void LinkNeighbors();
    void RebuildDirtyChunks();
};
