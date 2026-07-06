#pragma once
#include "globals.hpp"
#include "height_field.hpp"
#include "terrain_mesh_component.hpp"
#include <atomic>
#include <climits>
#include <functional>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

class Application;
class GameObject;
class HeightFieldColliderComponent;
class RigidbodyComponent;
class TerrainMaterial;

// Mutable HeightField backed by a plain grid — the collider ring re-fills it
// as tiles are (re)assigned, then calls SyncFromHeightField on the collider.
class StreamedHeightGrid : public HeightField {
public:
    int Width() const override {
        return _width;
    }
    int Depth() const override {
        return _depth;
    }
    float Sample(int xi, int zi) const override {
        return _grid[static_cast<size_t>(zi) * _width + xi];
    }
    const std::vector<float>& Grid() const override {
        return _grid;
    }
    void Assign(std::vector<float> grid, int width, int depth) {
        _grid = std::move(grid);
        _width = width;
        _depth = depth;
    }

private:
    int _width = 0, _depth = 0;
    std::vector<float> _grid;
};

// One streamed entity on the terrain (see TerrainStreamerProps::placeEntitiesFn).
struct TerrainEntityPlacement {
    glm::vec3 position{ 0.0f };// world space (set y from ctx.HeightAt)
    float yaw = 0.0f;// radians around world Y
    float scale = 1.0f;// uniform scale, applied by the streamer
    int type = 0;// user-defined kind; also the recycling-pool key
};

// Per-tile view handed to placeEntitiesFn. HeightAt samples the exact height
// source (not the LOD'd heightmap), so entity feet match LOD0 ground.
struct TerrainTileContext {
    glm::ivec2 coord{ 0, 0 };
    glm::vec2 worldMin{ 0.0f }, worldMax{ 0.0f };// tile rect, no gutter
    float heightScale = 0.0f;
    int seed = 0;// world seed — combine with coord for deterministic scatter
    const std::function<float(float, float)>* height01 = nullptr;
    float HeightAt(float wx, float wz) const {
        return (*height01)(wx, wz) * heightScale;
    }
};

// Open-world streaming terrain: a fixed worldSize x worldSize procedural
// terrain split into square tiles, all resident at some LOD (full view
// distance, no horizon pop-out) with concentric rings of increasing detail
// around the camera. Tile heightmaps are generated on JobSystem workers and
// uploaded within a per-frame budget, so refinement never blocks the frame.
//
// LOD L uses a (tileHeightRes >> L) heightmap and a (tileMeshRes >> L) patch
// grid; ring L covers tiles within Chebyshev distance lod0RadiusTiles << L.
// Tiles carry perimeter skirts so mixed-LOD borders never show cracks, and
// heightmaps include a 1-texel gutter so lighting is seam-free across tiles.
struct TerrainStreamerProps {
    float worldSize = 10240.0f;// full XZ extent in metres, centred on origin
    float tileSize = 512.0f;// metres per tile edge (worldSize is rounded to a multiple)
    float heightScale = 800.0f;// metres of displacement for height 1.0

    int tileHeightRes = 512;// LOD0 heightmap segments per tile edge (1 m/texel at 512m tiles)
    int tileMeshRes = 64;// LOD0 patch grid per tile edge (tessellation refines further)
    int lodCount = 4;// number of LOD rings
    int lod0RadiusTiles = 2;// Chebyshev radius of the LOD0 ring; ring L reaches lod0RadiusTiles << L
    float skirtDepth = 0.0f;// perimeter skirt drop in metres; 0 = auto per LOD
    int uploadsPerFrame = 4;// finished tiles integrated (GPU-uploaded) per Update()
    float tessellationFactor = 16.0f;
    int paletteIndex = 0;// fallback height-palette when no layers are set

    // Height source, sampled in world metres on worker threads (must be
    // thread-safe). Returns normalized height in [0,1]. When null, an
    // OpenSimplex2 FBm source is built from `noise` (frequency is cycles per
    // metre; `resolution` is ignored). Plug Gaea/WorldCreator tile pyramids or
    // any custom generator here.
    std::function<float(float wx, float wz)> heightFn;
    NoiseHeightFieldParams noise{ .resolution = 0, .seed = 1337, .frequency = 0.0004f, .octaves = 9 };

    // ── Surface maps (splat space, reserved for texturing passes) ──────────
    // Tiled detail layers shared by every tile. `tiling` here means texture
    // repeats per tile edge (world period = tileSize / tiling), continuous
    // across tile borders.
    std::vector<TerrainLayerDesc> layers;
    // Optional per-tile RGBA8 splat generator covering the given world rect
    // (rect includes the heightmap gutter so it lines up with the surface
    // UVs exactly). Runs on worker threads; return res*res*4 bytes, or empty
    // to skip the tile. When null, tiles use the automatic slope/height
    // weights (or the palette fallback when no layers are set either).
    std::function<std::vector<unsigned char>(glm::vec2 worldMin, glm::vec2 worldMax, int res)> splatFn;
    int splatRes = 256;

    // Bullet heightfield colliders for LOD0 tiles within this Chebyshev
    // radius of the camera tile (-1 disables physics entirely).
    int colliderRadiusTiles = 1;
    int colliderResolution = 129;// collider grid per tile (decimated from the LOD0 heightmap)

    // ── Entities (props/vegetation scatter, streamed with the tiles) ───────
    // Deterministic per-tile placements (called on the main thread; keep it
    // cheap — a few hundred HeightAt samples per tile). Entities exist for
    // tiles within entityRadiusTiles of the camera tile; tiles leaving the
    // ring return their GameObjects to per-type pools for reuse, so the
    // world-wide entity count is bounded by the ring area.
    std::function<std::vector<TerrainEntityPlacement>(const TerrainTileContext&)> placeEntitiesFn;
    // Build a fresh GameObject for a placement (mesh/components only — the
    // streamer applies position/yaw/scale/active and re-applies them when a
    // pooled object of the same type is recycled for a new placement).
    std::function<GameObject*(Application*, const TerrainEntityPlacement&)> spawnEntityFn;
    int entityRadiusTiles = 3;
    int entityTilesPerFrame = 1;// tiles populated per Update()
};

struct IVec2Hash {
    size_t operator()(const glm::ivec2& v) const noexcept {
        size_t h = std::hash<int>{}(v.x);
        h ^= std::hash<int>{}(v.y) + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};

class TerrainStreamer {
public:
    TerrainStreamer() = default;
    ~TerrainStreamer() = default;

    // root: all tile/collider GameObjects are parented here. Init synchronously
    // prewarms the whole world at the coarsest LOD (fast — a few hundred small
    // tiles in parallel) so the full view distance is visible on frame one;
    // the detail rings then stream in asynchronously.
    void Init(Application* app, const TerrainStreamerProps& props = {}, GameObject* root = nullptr);

    // Streams tiles toward their desired LOD and frustum-culls loaded tiles.
    // Call once per frame with the camera world position and view-projection.
    void Update(const glm::vec3& cameraPos, const glm::mat4& viewProj);

    // CPU height query in world metres (evaluates the height source directly).
    float GetHeight(float wx, float wz) const;

    // Debug: tint every tile by its LOD (palette index = LOD) so the detail
    // rings and their refinement become visible. Only affects tiles rendered
    // with the palette fallback (no layers / base map).
    void SetLodTintDebug(bool enabled);
    bool GetLodTintDebug() const {
        return _lodTintDebug;
    }

    struct Stats {
        int loadedTiles = 0;
        int visibleTiles = 0;
        int pendingJobs = 0;
        int tilesPerSide = 0;
        int activeEntities = 0;
        size_t gpuHeightmapBytes = 0;
        bool initialLoadDone = false;// every tile is at its desired LOD
    };
    const Stats& GetStats() const {
        return _stats;
    }
    const TerrainStreamerProps& Props() const {
        return _props;
    }

private:
    struct TileSlot {
        GameObject* go = nullptr;
        MeshHandle mesh;
        TerrainMaterial* material = nullptr;
        TextureHandle heightTex;
        int lod = 0;
        int slotIndex = 0;
        glm::ivec2 coord{ 0, 0 };
        std::vector<float> cpuGrid;// kept for LOD0 tiles only (collider source)
    };

    struct GenJob {
        glm::ivec2 coord{ 0, 0 };
        int lod = 0;
        std::vector<float> heights;// (n+3)^2 gutter grid
        std::vector<unsigned char> splat;// splatRes^2 RGBA8, empty if none
        std::atomic<bool> done{ false };
    };

    struct ColliderSlot {
        GameObject* go = nullptr;
        HeightFieldColliderComponent* collider = nullptr;
        RigidbodyComponent* rigidbody = nullptr;
        std::shared_ptr<StreamedHeightGrid> heightGrid;
        glm::ivec2 coord{ INT_MIN, INT_MIN };
    };

    // Per-LOD tile geometry.
    int HeightRes(int lod) const;// heightmap segments n (grid is (n+3)^2 with gutter)
    int MeshRes(int lod) const;
    float SkirtDepth(int lod) const;
    float GutterWorldSize(int lod) const;// world span covered by the gutter texture

    glm::ivec2 WorldToTile(const glm::vec3& worldPos) const;
    glm::vec2 TileOrigin(glm::ivec2 coord) const;// min-corner world XZ
    int DesiredLod(glm::ivec2 coord, glm::ivec2 camTile) const;
    int RingThreshold(int lod) const;

    void RequestTile(glm::ivec2 coord, int lod);
    // Integrates up to `budget` finished jobs (negative = unlimited).
    void IntegrateFinishedJobs(int budget, glm::ivec2 camTile);
    void ApplyJobToSlot(GenJob& job, TileSlot* slot);
    TileSlot* AcquireSlot(int lod);
    void ReleaseSlot(TileSlot* slot);
    void UpdateColliders(glm::ivec2 camTile);
    void AssignCollider(ColliderSlot& slot, TileSlot* tile);
    void UpdateEntities(glm::ivec2 camTile);
    void CullTiles(const glm::mat4& viewProj);

    TerrainStreamerProps _props;
    Application* _app = nullptr;
    GameObject* _root = nullptr;
    int _tilesPerSide = 0;
    int _maxInFlight = 8;

    std::function<float(float, float)> _heightFn;
    std::unordered_map<glm::ivec2, TileSlot*, IVec2Hash> _tiles;// loaded tiles
    std::unordered_map<glm::ivec2, std::shared_ptr<GenJob>, IVec2Hash> _inFlight;
    std::vector<std::vector<TileSlot*>> _pool;// recycled slots, per LOD
    std::vector<std::unique_ptr<TileSlot>> _allSlots;
    std::vector<ColliderSlot> _colliders;

    // Entity streaming: live per-tile spawn lists plus per-type recycle pools.
    struct SpawnedEntity {
        int type = 0;
        GameObject* go = nullptr;
    };
    std::unordered_map<glm::ivec2, std::vector<SpawnedEntity>, IVec2Hash> _entityTiles;
    std::unordered_map<int, std::vector<GameObject*>> _entityPool;
    int _activeEntities = 0;
    bool _lodTintDebug = false;

    // Layer textures resolved once in Init and shared by every tile material.
    struct ResolvedLayer {
        TextureHandle albedo;
        TextureHandle normal;
        float tilesPerTile = 32.0f;
    };
    std::vector<ResolvedLayer> _layers;

    Stats _stats;
};
