#pragma once
#include "Atmospheric.hpp"
#include "Atmospheric/camera_component.hpp"
#include "Atmospheric/portal_component.hpp"
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

        // Two large linked portals flanking the spawn point (camera starts at
        // (200, 80, 200) between them), facing each other — the classic
        // infinite-corridor recursion. Fly into either disc to teleport out of
        // the other, Portal-style.
        auto* blueGO = gameObject->GetApp()->CreateGameObject(glm::vec3(185.0f, 76.0f, 200.0f));
        blueGO->SetName("PortalBlue");
        blueGO->SetRotation(glm::vec3(0.0f, glm::radians(90.0f), 0.0f));// +Z -> +X, faces the spawn
        auto* blue = static_cast<PortalComponent*>(blueGO->AddComponent<PortalComponent>(PortalProps{
            .radius = 8.0f,
            .rimColor = { 0.25f, 0.55f, 1.0f },
        }));

        auto* orangeGO = gameObject->GetApp()->CreateGameObject(glm::vec3(215.0f, 76.0f, 200.0f));
        orangeGO->SetName("PortalOrange");
        orangeGO->SetRotation(glm::vec3(0.0f, glm::radians(-90.0f), 0.0f));// +Z -> -X, faces the spawn
        auto* orange = static_cast<PortalComponent*>(orangeGO->AddComponent<PortalComponent>(PortalProps{
            .radius = 8.0f,
            .rimColor = { 1.0f, 0.55f, 0.15f },
        }));

        PortalComponent::Link(blue, orange);
    }
    void OnTick(float dt) override {
        if (!_camera) return;
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
