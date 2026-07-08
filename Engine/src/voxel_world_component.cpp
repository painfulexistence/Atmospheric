#include "voxel_world_component.hpp"
#include "application.hpp"
#include "asset_manager.hpp"
#include "camera_component.hpp"
#include "game_object.hpp"
#include "graphics_subsystem.hpp"
#include "imgui.h"
#include "material.hpp"

VoxelWorldComponent::VoxelWorldComponent(GameObject* go, int seed) : _seed(seed) {
    gameObject = go;
}

void VoxelWorldComponent::OnAttach() {
    // Own the material per world so multi-world scenes can hold distinct
    // palettes concurrently; _world.paletteIndex is the source of truth and
    // OnTick pushes it into the material each frame for the pass to read.
    auto& assets = AssetManager::Get();
    VoxelMaterial* vm = assets.CreateVoxelMaterial();
    _voxelMatHandle = assets.GetMaterialHandle(vm);
    _world.Init(gameObject->GetApp(), _seed, gameObject, _voxelMatHandle);
    _camera = gameObject->GetApp()->GetMainCamera();
}

void VoxelWorldComponent::OnTick(float dt) {
    if (!_camera) return;

    if (auto* vm = dynamic_cast<VoxelMaterial*>(AssetManager::Get().ResolveMaterial(_voxelMatHandle))) {
        vm->paletteIndex = _world.paletteIndex;
    }

    glm::vec3 pos = _camera->gameObject->GetPosition();
    glm::mat4 viewProj = _camera->GetProjectionMatrix() * _camera->GetViewMatrix();
    _world.Update(dt, pos);
    _world.SubmitRenderCommands(GraphicsSubsystem::Get()->renderer.get(), viewProj, pos);
}

void VoxelWorldComponent::DrawImGui() {
    ImGui::Checkbox("Infinite Streaming", &_world.infiniteMode);
    static const char* gpaletteNames[] = { "1 - Warm Pink/Gold", "2 - Cool Blue/Purple",    "3 - Earthy Green",
                                           "4 - Forest",         "5 - Soft Cool (default)", "6 - Vivid Mint/Coral" };
    ImGui::Combo("Palette", &_world.paletteIndex, gpaletteNames, 6);
}
