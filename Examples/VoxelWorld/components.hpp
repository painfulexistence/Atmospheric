#pragma once
#include "Atmospheric.hpp"
#include "Atmospheric/camera_component.hpp"
#include "Atmospheric/voxel_world.hpp"
#include "Atmospheric/water_component.hpp"
#include "imgui.h"

// World-axis fly camera. WASD move, R/F up/down, IJKL pitch/yaw.
class FlyCameraComponent : public Component {
    float _moveSpeed, _lookSpeed;
    CameraComponent* _camera = nullptr;
public:
    FlyCameraComponent(GameObject* go, float moveSpeed = 20.0f, float lookSpeed = 1.5f)
        : _moveSpeed(moveSpeed), _lookSpeed(lookSpeed) { gameObject = go; }
    std::string GetName() const override { return "FlyCameraComponent"; }
    void OnAttach() override { _camera = gameObject->GetComponent<CameraComponent>(); }
    void OnTick(float dt) override {
        auto* input = gameObject->GetApp()->GetInput();
        if (!input) return;
        const float move = _moveSpeed * dt, look = _lookSpeed * dt;
        if (_camera) {
            if (input->IsKeyDown(Key::I)) _camera->Pitch(look);
            if (input->IsKeyDown(Key::K)) _camera->Pitch(-look);
            if (input->IsKeyDown(Key::J)) _camera->Yaw(-look);
            if (input->IsKeyDown(Key::L)) _camera->Yaw(look);
        }
        glm::vec3 pos = gameObject->GetPosition();
        if (input->IsKeyDown(Key::W)) pos.z -= move;
        if (input->IsKeyDown(Key::S)) pos.z += move;
        if (input->IsKeyDown(Key::A)) pos.x -= move;
        if (input->IsKeyDown(Key::D)) pos.x += move;
        if (input->IsKeyDown(Key::R)) pos.y += move;
        if (input->IsKeyDown(Key::F)) pos.y -= move;
        gameObject->SetPosition(pos);
    }
};

// Owns the voxel world: initialises it on attach, streams chunks and submits
// render commands each tick relative to the main camera position.
class VoxelWorldComponent : public Component {
    VoxelWorld     _world;
    int            _seed;
    CameraComponent* _camera  = nullptr;
    GameObject*    _waterGO   = nullptr;
public:
    float waterLine = 32.0f;

    explicit VoxelWorldComponent(GameObject* go, int seed = 42)
        : _seed(seed) { gameObject = go; }
    std::string GetName() const override { return "VoxelWorld"; }
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
            .width        = waterExt,
            .depth        = waterExt,
            .subdivisions = 64,
        });
    }
    void OnTick(float dt) override {
        if (!_camera) return;
        glm::vec3 pos     = _camera->gameObject->GetPosition();
        glm::mat4 viewProj = _camera->GetProjectionMatrix() * _camera->GetViewMatrix();
        _world.Update(dt, pos);
        _world.SubmitRenderCommands(
            gameObject->GetApp()->GetGraphicsServer()->renderer, viewProj, pos
        );

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
