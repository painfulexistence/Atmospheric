#include "mesh_renderer_component.hpp"
#include "application.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "material.hpp"
#include "mesh.hpp"

MeshRendererComponent::MeshRendererComponent(GameObject* gameObject, MeshHandle mesh) {
    this->_mesh = mesh;
}

MeshRendererComponent::~MeshRendererComponent() {
}

std::string MeshRendererComponent::GetName() const {
    return std::string("MeshRendererComponent");
}

void MeshRendererComponent::OnAttach() {
    if (GraphicsSubsystem::Get()) {
        GraphicsSubsystem::Get()->RegisterMesh(this);
    }
}

void MeshRendererComponent::OnDetach() {
    if (gameObject && gameObject->GetApp() && GraphicsSubsystem::Get()) {
        GraphicsSubsystem::Get()->UnregisterMesh(this);
    }
}

MeshHandle MeshRendererComponent::GetMesh() const {
    return _mesh;
}

void MeshRendererComponent::SetMesh(MeshHandle mesh) {
    _mesh = mesh;
}

Material* MeshRendererComponent::GetMaterial() const {
    auto& assets = AssetManager::Get();
    if (Material* mat = assets.ResolveMaterial(_material)) {
        return mat;
    }
    Mesh* meshPtr = assets.GetMeshPtr(_mesh);
    return meshPtr ? assets.ResolveMaterial(meshPtr->GetMaterial()) : nullptr;
}

void MeshRendererComponent::SetMaterial(MaterialHandle material) {
    _material = material;
}

void MeshRendererComponent::DrawImGui() {
    if (auto* mat = GetMaterial()) mat->DrawImGui();
}