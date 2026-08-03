#pragma once
#include "component.hpp"
#include <climits>
#include <cstdint>
#include <glm/glm.hpp>
#include <shared_mutex>
#include <string>
#include <vector>

// What a VoxelVolumeComponent generates. Terrain is the original 12.8m diorama
// (heightfield + caves + ore + crystals + glowstones); the object kinds are
// small self-contained props (crates, boulders, crystal clusters) meant for
// grids of 16-48 voxels per edge, so a scene can mix one big terrain volume
// with many cheap per-object volumes. All kinds share one material palette.
enum class VoxelVolumeKind : uint8_t {
    Terrain,
    Crate,
    Boulder,
    CrystalCluster,
};

// A micro voxel volume attached to a GameObject. Owns the CPU-side voxel data
// (a dense palette-index grid + a coarse occupancy grid + a palette) and its
// procedural generation. MicroVoxelPass renders every attached volume by
// reading this data and uploading it to the GPU. The volume follows the full
// GameObject transform (translation + rotation; scale must stay 1 — the
// raymarch assumes a rigid transform): the grid is centred over the object in
// x/z and rises from its base in local space, and the shaders march rays in
// this local space. Contrast with VoxelChunkComponent, which is the macro
// (greedy-meshed, triangle) voxel world.
class VoxelVolumeComponent : public Component {
public:
    explicit VoxelVolumeComponent(GameObject* owner, uint32_t seed = 1337u);
    // Object-kind volumes pick their own grid size (16-48 is the useful range;
    // rounded down to a multiple of brickDim by Generate).
    VoxelVolumeComponent(GameObject* owner, uint32_t seed, int gridDimIn, VoxelVolumeKind kindIn);

    std::string GetName() const override {
        return "VoxelVolume";
    }
    void OnAttach() override;// generate + register with MicroVoxelPass
    void OnDetach() override;// unregister
    void DrawImGui() override;

    // (Re)build the procedural volume for this component's kind on the CPU and
    // flag the whole grid for GPU re-upload (fullDirty).
    void Generate(uint32_t seed);

    // Runtime editing (destruction). CarveSphere clears voxels to air inside a
    // world-space sphere (transformed into the volume's local space, so it
    // works on rotated volumes), recomputes the affected occupancy bricks and
    // the tight solid bounds, and records the touched voxel region so the pass
    // re-uploads only that sub-box. No remeshing is needed — the strength of
    // the raymarch model.
    void CarveSphere(const glm::vec3& worldCenter, float radius);

    // First solid voxel along a world-space ray (for aiming a carve). The ray
    // is transformed into the volume's local space, so rotated volumes work.
    // Returns the world hit position in outHitWorld; false on a miss within
    // maxDist.
    bool RaycastVoxel(const glm::vec3& worldRo, const glm::vec3& worldRd, float maxDist, glm::vec3& outHitWorld) const;

    // ---- Collision geometry -------------------------------------------------
    // Both emit points in the volume's LOCAL space — the same frame the
    // raymarch marches in, whose origin is the GameObject's pivot. A Bullet
    // shape built from these can be attached to a body placed at the object
    // transform with no further offset, so collider and visuals agree,
    // rotation included.

    // Triangle mesh of every exposed face, greedy-merged into maximal
    // rectangles per slice, with outward winding. Feeds a static
    // btBvhTriangleMeshShape (Bullet's triangle meshes cannot move), so it is
    // the terrain path. Empty regions are skipped a whole brick at a time via
    // the occupancy grid.
    //
    // `step` coarsens the grid the mesh is built from: 1 is voxel-exact, 2/4/8
    // treat each step^3 block as one cell that is solid if ANY voxel in it is
    // (conservative — the collider never gains holes, it only inflates outward
    // by up to step-1 voxels). Triangle count falls roughly with step^2, which
    // matters because narrowphase cost tracks it: a 256^3 terrain is ~237k
    // triangles at step 1 and a fraction of that at step 2. Values are rounded
    // to a power of two in [1, 8] so a coarse cell always sits inside one
    // brick, keeping the occupancy fast path valid.
    void BuildSurfaceMesh(std::vector<glm::vec3>& outVertices, std::vector<uint32_t>& outIndices, int step = 1) const;

    // As above, but restricted to voxels inside [regionMin, regionMax]
    // (inclusive, voxel coordinates, clamped to the solid bounds). Whether a
    // face on the region's own boundary is emitted is still decided against the
    // voxels OUTSIDE it, so meshing a grid in pieces yields exactly the same
    // surface as meshing it whole: no seams, and no internal walls where two
    // pieces meet. That is what lets a collider be rebuilt one chunk at a time
    // after an edit instead of re-meshing the whole volume.
    //
    // The region should be aligned to `step` (any multiple of brickDim is,
    // since step is rounded down to a power-of-two divisor of brickDim);
    // otherwise coarse cells straddle the boundary and neighbouring regions
    // both emit them.
    void BuildSurfaceMeshRegion(
        std::vector<glm::vec3>& outVertices,
        std::vector<uint32_t>& outIndices,
        const glm::ivec3& regionMin,
        const glm::ivec3& regionMax,
        int step = 1
    ) const;

    // Support points of the solid voxels over `directions` roughly even
    // directions — a small bounded set whose convex hull approximates (and is
    // inscribed in) the true hull, for a dynamic btConvexHullShape. Intended
    // for props; a whole terrain would hull to something useless.
    void BuildConvexHullPoints(std::vector<glm::vec3>& outPoints, int directions = 64) const;

    // The extractors above and the raw voxel arrays are NOT internally locked:
    // meshing walks millions of cells and paying for a lock per voxel would be
    // absurd. Anything reading them from another thread must hold this shared
    // lock for the whole read instead — VoxelColliderComponent takes it once
    // around an entire extraction. CarveSphere and Generate take the matching
    // exclusive lock, so an edit cannot land halfway through a mesh build.
    // Without it, holding a dig key carves on the main thread while a worker is
    // reading the same voxels.
    [[nodiscard]] std::shared_lock<std::shared_mutex> LockVoxelsShared() const {
        return std::shared_lock<std::shared_mutex>(_voxelMutex);
    }

    // Local-space min corner: the grid is centred over the object in x/z and
    // rises from y=0, i.e. (-half, 0, -half).
    glm::vec3 GetLocalOrigin() const;

    // Centroid of the solid voxels in local space — the centre of mass, since
    // voxels are uniform. Note this is NOT the local origin: that is the
    // grid's bottom centre, so a prop's centre of mass sits roughly half its
    // height above it. Physics needs the difference (Bullet takes a body's
    // origin to be its centre of mass), and it is well away from zero for
    // anything whose voxels do not fill the grid symmetrically. Returns zero
    // if nothing is solid. O(solid bounds) with brick skipping — cheap for a
    // prop, a full scan for terrain, so call it once at build time.
    glm::vec3 GetSolidCentroidLocal() const;

    // World-space min corner, valid only while the object is unrotated (kept
    // for the WebGPU single-volume path, which has no OBB support yet).
    glm::vec3 GetOrigin() const;

    // The volume's rigid local->world transform (the owning GameObject's world
    // transform; scale is expected to be 1).
    glm::mat4 GetModelMatrix() const;

    float WorldExtent() const {
        return static_cast<float>(gridDim) * voxelSize;
    }

    // Tight solid bounds in voxel coordinates (inclusive), maintained from the
    // occupancy grid by Generate and CarveSphere. The pass rasterizes and
    // slab-tests this box instead of the full grid, so mostly-empty volumes
    // don't pay for their air. Empty volume => solidMax < solidMin.
    bool HasSolid() const {
        return solidCount > 0 && solidMax.x >= solidMin.x;
    }
    // Bounds in local space, expanded by half a voxel of slack so rays entering
    // exactly on the rasterized box surface can't miss the slab by an epsilon.
    glm::vec3 GetLocalBoundsMin() const;
    glm::vec3 GetLocalBoundsMax() const;

    // Grid config (gridDim must be a multiple of brickDim; Generate enforces it).
#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
    // 9.6 m at 5 cm voxels — WebGL2 and mobile GLES fragment raymarch is
    // pricier per pixel, and phones also pay 3D-texture memory for each volume.
    int gridDim = 192;
#else
    int gridDim = 256;// 12.8 m at 5 cm voxels
#endif
    int brickDim = 8;
    float voxelSize = 0.05f;// 5 cm voxels => 256^3 is a 12.8 m diorama
    uint32_t seed = 1337u;
    VoxelVolumeKind kind = VoxelVolumeKind::Terrain;

    // CPU data, consumed by MicroVoxelPass for GPU upload.
    std::vector<uint8_t> volume;// gridDim^3 palette indices, 0 = air
    std::vector<uint8_t> occupancy;// (gridDim/brickDim)^3, nonzero = brick occupied
    std::vector<uint8_t> paletteRGBA;// 256x2 RGBA8: row0 albedo+emission, row1 reflectivity/roughness
    uint32_t solidCount = 0;
    glm::ivec3 solidMin{ 0 };// tight solid bounds (voxel coords, inclusive)
    glm::ivec3 solidMax{ -1 };

    // Upload state, consumed and reset by MicroVoxelPass. fullDirty means the
    // grid was (re)generated: (re)allocate the textures, upload everything, and
    // reset GI history. Otherwise dirtyMin/dirtyMax bound the edited voxels and
    // the pass uploads just that sub-box (a carve is ~KBs instead of the full
    // 16.8 MB at 256^3) and keeps GI history — the GI pass's per-pixel distance
    // validation rejects stale samples where the surface actually changed.
    bool dirty = true;
    bool fullDirty = true;
    glm::ivec3 dirtyMin{ INT_MAX };
    glm::ivec3 dirtyMax{ INT_MIN };
    bool HasDirtyRegion() const {
        return dirtyMax.x >= dirtyMin.x;
    }
    void ClearDirty() {
        dirty = false;
        fullDirty = false;
        dirtyMin = glm::ivec3(INT_MAX);
        dirtyMax = glm::ivec3(INT_MIN);
    }

private:
    // Guards the voxel/occupancy arrays against edits landing mid-read. Only
    // ever contended when an edit coincides with a collider rebuild.
    mutable std::shared_mutex _voxelMutex;

    void _buildPalette();
    void _generateTerrain();
    void _generateCrate();
    void _generateBoulder();
    void _generateCrystalCluster();
    // Recompute occupancy for an inclusive brick range from the voxel grid.
    void _recomputeOccupancyRange(const glm::ivec3& brickLo, const glm::ivec3& brickHi);
    // Recompute solidMin/solidMax by scanning the (small) occupancy grid.
    void _recomputeSolidBounds();
    void _markRegionDirty(const glm::ivec3& lo, const glm::ivec3& hi);
};
