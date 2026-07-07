#pragma once
#include "Atmospheric.hpp"
#include "Atmospheric/camera_component.hpp"
#include "Atmospheric/voxel_world.hpp"
#include "Atmospheric/water_component.hpp"
#include "imgui.h"

// Owns the voxel world: initialises it on attach, streams chunks and submits
// render commands each tick relative to the main camera position.
class VoxelWorldComponent : public Component {
    VoxelWorld _world;
    int _seed;
    CameraComponent* _camera = nullptr;
    GameObject* _waterGO = nullptr;

public:
    float waterLine = 32.0f;

    explicit VoxelWorldComponent(GameObject* go, int seed = 42) : _seed(seed) {
        gameObject = go;
    }
    std::string GetName() const override {
        return "VoxelWorld";
    }
    void OnAttach() override {
        _world.Init(gameObject->GetApp(), _seed, gameObject);
        _camera = gameObject->GetApp()->GetMainCamera();

        // Water plane — large enough to cover the full view range plus fog horizon.
        // waterLine is left at its sentinel so WaterComponent reads it back from
        // this GameObject's own Y position, making the position below the only
        // place the water height is set.
        const float waterExt = ((2 * VoxelWorld::VIEW_X + 1) * VoxelChunkComponent::SIZE) * 2.0f;
        _waterGO = gameObject->GetApp()->CreateGameObject(glm::vec3(0.0f, waterLine + 0.05f, 0.0f));
        _waterGO->SetName("VoxelWater");
        _waterGO->parent = gameObject;
        _waterGO->AddComponent<WaterComponent>(WaterProps{
            .width = waterExt,
            .depth = waterExt,
            .subdivisions = 64,
        });
    }
    void OnTick(float dt) override {
        if (!_camera) return;

        // Hold E to dig — the same control as the MicroVoxel example, for a
        // direct contrast. Here carving marks the affected chunks dirty and
        // _world.Update() below RE-MESHES them (greedy meshing rebuilds the
        // vertex buffers); the micro-voxel path instead just re-uploads a 3D
        // texture with no geometry rebuild.
        if (InputSubsystem::Get()->IsKeyDown(Key::E)) {
            const glm::vec3 ro = _camera->GetEyePosition();
            const glm::vec3 rd = _camera->GetEyeDirection();
            glm::vec3 hit;
            if (_world.RaycastVoxel(ro, rd, 200.0f, hit)) {
                _world.CarveSphere(hit, 3.0f);// 3 m crater (1 m voxels)
            }
        }

        glm::vec3 pos = _camera->gameObject->GetPosition();
        glm::mat4 viewProj = _camera->GetProjectionMatrix() * _camera->GetViewMatrix();
        _world.Update(dt, pos);
        _world.SubmitRenderCommands(GraphicsSubsystem::Get()->renderer.get(), viewProj, pos);

        // Slide the water plane with the camera so it always covers the visible area.
        if (_waterGO) {
            _waterGO->SetPosition(glm::vec3(pos.x, waterLine + 0.05f, pos.z));
        }
    }
    void DrawImGui() override {
        ImGui::Checkbox("Infinite Streaming", &_world.infiniteMode);
        ImGui::DragFloat("Water Line", &waterLine, 0.5f);
    }
};
