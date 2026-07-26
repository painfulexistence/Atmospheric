#pragma once
#include "component.hpp"
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

class VoxelVolumeComponent;
class btCollisionShape;
class btTriangleIndexVertexArray;

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
    void DrawImGui() override;

    // Rebuild the shape from the current voxels (e.g. after a big edit) and
    // replace the rigid body. Cheap for a prop, a full re-mesh for terrain.
    void Rebuild();

    const VoxelColliderProps& GetProps() const {
        return _props;
    }

private:
    void _build();
    void _releaseShape();

    VoxelColliderProps _props;
    // Kept alive for Bullet, which stores raw pointers to these.
    std::vector<glm::vec3> _vertices;
    std::vector<uint32_t> _indices;
    std::unique_ptr<btTriangleIndexVertexArray> _meshInterface;
    std::unique_ptr<btCollisionShape> _shape;
    int _triangleCount = 0;
    int _hullPointCount = 0;
};
