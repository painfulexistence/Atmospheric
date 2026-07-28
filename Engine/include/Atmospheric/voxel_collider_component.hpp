#pragma once
#include "component.hpp"
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

class VoxelVolumeComponent;
class btCollisionShape;
class btTriangleIndexVertexArray;
struct btTriangleInfoMap;

struct VoxelColliderProps {
    // false: a static triangle mesh of the volume's exposed voxel faces
    //        (Bullet's mesh shapes cannot move) — the terrain case.
    // true:  a convex hull of the solid voxels, which a moving body needs.
    bool dynamic = false;
    // Mass for the dynamic case. <= 0 derives it from the solid volume:
    // solidCount * voxelSize^3 * density.
    float mass = 0.0f;
    float density = 500.0f;// kg/m^3
    int hullDirections = 64;// support directions sampled for the hull
    float friction = 0.8f;
    float restitution = 0.1f;
    // Coarsening for the static mesh (1 = voxel-exact, 2/4/8 progressively
    // cheaper). Narrowphase cost tracks the triangle count, and voxel-exact
    // terrain is dense: a 256^3 volume is ~237k triangles at 1, ~57k at 2 and
    // ~12k at 4. Coarsening only inflates the collider outward (never holes),
    // at the cost of props resting up to step-1 voxels above the visual
    // surface. Ignored by the hull path.
    int meshDownsample = 1;
    // Split the static mesh into a grid of chunks this many voxels on a side,
    // each its own body (0 = a single shape for the whole volume). This is what
    // makes editing affordable: carving re-meshes only the chunks it touched,
    // around a millisecond each, instead of the whole volume — 60 ms at 256^3
    // and downsample 2, which is unusable while a dig key is held down.
    // Costs a few percent more triangles, since greedy runs cannot span chunk
    // boundaries, plus one body per non-empty chunk. Must be a multiple of
    // brickDim; ignored by the dynamic hull path, which is small enough to
    // rebuild whole.
    int chunkVoxels = 0;
    // Collision margin in meters. 0 derives one from the voxel size. Bullet's
    // default is 4 cm, which is CATASTROPHIC here: it is comparable to a voxel,
    // so every triangle inflates into its neighbours, contact normals contradict
    // each other, bodies jitter and sink, and props visibly float a voxel above
    // the ground. Sub-voxel is what this scale needs.
    float collisionMargin = 0.0f;
    // Sweep fast-moving bodies instead of only testing their end pose, so a
    // prop dropped from height cannot pass through the terrain surface.
    bool continuousCollision = true;
    // Building the mesh is O(surface area) and runs inline on attach, so a huge
    // volume would stall a frame. Volumes with more grid voxels than this get
    // no mesh collider (0 = no limit). The default admits a 256^3 terrain
    // (16.7M) and rejects anything an order of magnitude past it.
    uint64_t maxMeshVoxels = 32ull * 1024ull * 1024ull;
    // Carving a prop away: once its remaining solid voxels fall below this
    // fraction of what it was built with, it is considered destroyed and
    // OnDestroyed fires (0 disables). Note this is deliberately NOT "rebuild
    // the hull and carry on" — a convex hull is the object's outer envelope,
    // so hollowing a crate out does not change it at all, and no amount of
    // rebuilding will let you fall through the hole. Erode the silhouette,
    // then break the thing. Dynamic bodies only.
    float destroyBelowSolidFraction = 0.4f;
    // Above that threshold, re-hull after this much of the ORIGINAL volume has
    // been carved away since the last rebuild, so the silhouette follows
    // corners being knocked off without re-hulling on every dig frame
    // (0 disables). Dynamic bodies only.
    float rehullAfterSolidFraction = 0.08f;
};

// Gives a voxel volume a physics body whose collider comes from its own voxels.
// Attach AFTER the VoxelVolumeComponent (which generates on attach): this reads
// the finished voxel data, builds the Bullet shape, and adds a
// RigidbodyComponent to the same object.
//
// The volume's local frame is already centred on the object's pivot, so the
// shape needs no offset — the body sits at the object transform and the
// collider lines up with what the raymarch draws, rotation included. Bullet
// writes each non-kinematic body's pose back to the GameObject every frame
// (Application::Update), and the raymarch reads that transform, so a tumbling
// prop renders tumbling.
//
// Owns the shape and, for the mesh case, the vertex/index arrays and the
// triangle interface — Bullet keeps raw pointers into all of them.
class VoxelColliderComponent : public Component {
public:
    explicit VoxelColliderComponent(GameObject* owner, const VoxelColliderProps& props = {});
    // Out-of-line so the unique_ptrs see the complete Bullet types.
    ~VoxelColliderComponent() override;

    std::string GetName() const override {
        return "VoxelCollider";
    }
    void OnAttach() override;
    void OnDetach() override;
    void OnTick(float dt) override;
    void DrawImGui() override;

    // Rebuild everything from the current voxels. Cheap for a prop, a full
    // re-mesh for terrain — prefer MarkDirtyRegion after an edit, which only
    // redoes the chunks that actually changed.
    void Rebuild();

    // Queue a rebuild of just the chunks overlapping this voxel-space box
    // (inclusive), which is what a carve should call: VoxelVolumeComponent
    // already tracks exactly this region for its partial GPU upload, so the
    // same box drives both. Coalesces — marking during an in-flight build
    // simply rebuilds again afterwards, so holding a dig key down costs one
    // rebuild per completed pass rather than one per frame. No-op on an
    // unchunked collider's dynamic hull, which Rebuild handles whole.
    void MarkDirtyRegion(const glm::ivec3& voxelMin, const glm::ivec3& voxelMax);

    // Chunks still waiting to be re-meshed. Zero means the collider matches
    // the voxels.
    int GetDirtyChunkCount() const;

    // Triangles across every live chunk of the static mesh. Changes whenever an
    // edit is actually reflected in the collider, which makes it the cheapest
    // way to confirm a dug hole reached physics and not just the renderer.
    int GetTriangleCount() const {
        return _triangleCount;
    }

    // Called once, on the frame a dynamic prop's remaining voxels drop below
    // destroyBelowSolidFraction. The collider does not delete anything itself
    // — what "destroyed" means (despawn, spawn debris, swap in a broken
    // variant) belongs to the game, not the engine. The component stops
    // simulating meaningfully after this: it is expected to be removed.
    std::function<void(VoxelColliderComponent&)> OnDestroyed;

    bool IsDestroyed() const {
        return _destroyed;
    }
    // Solid voxels the collider was last built from, and what the volume
    // started with — the ratio is what the destroy threshold tests.
    uint32_t GetInitialSolidCount() const {
        return _initialSolidCount;
    }

    // True from the moment a build is queued until its body is attached. The
    // object has no rigid body during this window, so anything that must not
    // start simulating before the world is collidable can gate on it — a demo
    // holding its props until the terrain's mesh lands, for instance.
    bool IsBuilding() const {
        return _pending != nullptr;
    }

    const VoxelColliderProps& GetProps() const {
        return _props;
    }

private:
    // Extraction result, filled on a worker thread and consumed on the main
    // thread once `done` flips. Held by shared_ptr so the worker's copy keeps
    // it alive even if the component is torn down mid-build.
    struct PendingBuild;
    // One piece of the static mesh, with its own body. Separate bodies rather
    // than a btCompoundShape on purpose: Bullet's internal-edge correction
    // casts the BODY's root shape to btBvhTriangleMeshShape to reach its
    // triangle info map, so a compound root would have it reinterpreting
    // unrelated memory. An unchunked collider is simply one chunk covering
    // the whole grid.
    struct Chunk;

    void _beginBuild();
    void _finishBuild();
    // Blocks until any in-flight extraction finishes. Required before the
    // voxel volume it reads can be destroyed.
    void _waitForBuild();
    // Runs on a worker thread: reads voxels, produces the Bullet shapes.
    // Static and taking everything by parameter so it cannot touch component
    // state the main thread owns.
    static void _extract(PendingBuild& out, const VoxelVolumeComponent& volume, const VoxelColliderProps& props);
    // Lays out the chunk grid over the volume. Called once, before any build.
    void _initChunks(const VoxelVolumeComponent& volume);
    // Dynamic props only: fires OnDestroyed once the remaining voxels fall
    // past the threshold, otherwise re-hulls when enough has been carved
    // away to change the silhouette.
    void _checkDestruction();
    void _releaseChunk(Chunk& chunk);
    void _releaseAll();

    VoxelColliderProps _props;
    std::vector<Chunk> _chunks;
    // Dynamic props keep the single-shape path: one hull, one body.
    std::unique_ptr<btCollisionShape> _hull;
    std::shared_ptr<PendingBuild> _pending;
    // Chunk indices queued for re-meshing, including any marked while a build
    // was already running.
    std::vector<int> _dirtyChunks;
    // Solid centroid the dynamic hull was built about, in the object's local
    // frame. Zero for the static mesh, which keeps that frame as-is.
    glm::vec3 _centerOfMass{ 0.0f };
    int _triangleCount = 0;
    int _hullPointCount = 0;
    // Destruction bookkeeping for dynamic props: what the volume held when the
    // collider was first built, what it held at the last hull rebuild, and
    // whether OnDestroyed has already fired (it fires once).
    uint32_t _initialSolidCount = 0;
    uint32_t _solidCountAtLastBuild = 0;
    bool _destroyed = false;
};
