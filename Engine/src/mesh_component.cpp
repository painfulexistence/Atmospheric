#include "mesh_component.hpp"
#include "application.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "material.hpp"
#include "mesh.hpp"

MeshComponent::MeshComponent(GameObject* gameObject, MeshHandle mesh) {
    this->_mesh = mesh;
}

MeshComponent::~MeshComponent() {
}

std::string MeshComponent::GetName() const {
    return std::string("Drawable");
}

void MeshComponent::OnAttach() {
    if (GraphicsSubsystem::Get()) {
        GraphicsSubsystem::Get()->RegisterMesh(this);
    }
}

void MeshComponent::OnDetach() {
    if (gameObject && gameObject->GetApp() && GraphicsSubsystem::Get()) {
        GraphicsSubsystem::Get()->UnregisterMesh(this);
    }
}

MeshHandle MeshComponent::GetMesh() const {
    return _mesh;
}

void MeshComponent::SetMesh(MeshHandle mesh) {
    _mesh = mesh;
}

Material* MeshComponent::GetMaterial() const {
    auto& assets = AssetManager::Get();
    if (Material* mat = assets.ResolveMaterial(_material)) {
        return mat;
    }
    Mesh* meshPtr = assets.GetMeshPtr(_mesh);
    return meshPtr ? assets.ResolveMaterial(meshPtr->GetMaterial()) : nullptr;
}

void MeshComponent::SetMaterial(MaterialHandle material) {
    _material = material;
}

void MeshComponent::DrawImGui() {
    if (auto* mat = GetMaterial()) mat->DrawImGui();
}