#pragma once
#include "component.hpp"
#include "globals.hpp"
#include "voxel_world.hpp"

class CameraComponent;

// Adapts VoxelWorld (a plain streaming/meshing data class) to the component
// lifecycle: creates the per-world VoxelMaterial in OnAttach, drives Update /
// SubmitRenderCommands each tick from the main camera, and syncs the
// world-owned palette selection onto the material so VoxelChunkPass can read
// a live value off each render command.
//
// Anything the demo layer decides — water plane, sky tuning, camera setup —
// stays out of here and lives in the example's main.
class VoxelWorldComponent : public Component {
public:
    explicit VoxelWorldComponent(GameObject* go, int seed = 42);
    std::string GetName() const override {
        return "VoxelWorld";
    }
    void OnAttach() override;
    void OnDetach() override;
    void OnTick(float dt) override;
    void DrawImGui() override;

    VoxelWorld& World() {
        return _world;
    }
    MaterialHandle GetMaterial() const {
        return _voxelMatHandle;
    }

private:
    VoxelWorld _world;
    int _seed;
    CameraComponent* _camera = nullptr;
    MaterialHandle _voxelMatHandle;
};
