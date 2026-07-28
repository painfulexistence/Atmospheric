#pragma once
#include "component.hpp"
#include <cstdint>
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

    // Rebuild the shape from the current voxels (e.g. after a big edit) and
    // replace the rigid body. Cheap for a prop, a full re-mesh for terrain.
    void Rebuild();

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

    void _beginBuild();
    void _finishBuild();
    // Blocks until any in-flight extraction finishes. Required before the
    // voxel volume it reads can be destroyed.
    void _waitForBuild();
    // Runs on a worker thread: reads voxels, produces the Bullet shape. Static
    // and taking everything by parameter so it cannot touch component state
    // the main thread owns.
    static void _extract(PendingBuild& out, const VoxelVolumeComponent& volume, const VoxelColliderProps& props);
    void _releaseShape();

    VoxelColliderProps _props;
    // Kept alive for Bullet, which stores raw pointers to these.
    std::vector<glm::vec3> _vertices;
    std::vector<uint32_t> _indices;
    std::unique_ptr<btTriangleIndexVertexArray> _meshInterface;
    // Triangle adjacency for the internal-edge fix; the shape only borrows it.
    std::unique_ptr<btTriangleInfoMap> _triangleInfo;
    std::unique_ptr<btCollisionShape> _shape;
    std::shared_ptr<PendingBuild> _pending;
    // Solid centroid the dynamic hull was built about, in the object's local
    // frame. Zero for the static mesh, which keeps that frame as-is.
    glm::vec3 _centerOfMass{ 0.0f };
    int _triangleCount = 0;
    int _hullPointCount = 0;
};
