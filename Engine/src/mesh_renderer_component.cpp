#include "mesh_renderer_component.hpp"
#include "application.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "material.hpp"
#include "mesh.hpp"

MeshRenderer::MeshRenderer(GameObject* gameObject, MeshHandle mesh) {
    this->_mesh = mesh;
}

MeshRenderer::~MeshRenderer() {
}

std::string MeshRenderer::GetName() const {
    return std::string("MeshRenderer");
}

void MeshRenderer::OnAttach() {
    if (GraphicsSubsystem::Get()) {
        GraphicsSubsystem::Get()->RegisterMesh(this);
    }
}

void MeshRenderer::OnDetach() {
    if (gameObject && gameObject->GetApp() && GraphicsSubsystem::Get()) {
        GraphicsSubsystem::Get()->UnregisterMesh(this);
    }
}

MeshHandle MeshRenderer::GetMesh() const {
    return _mesh;
}

void MeshRenderer::SetMesh(MeshHandle mesh) {
    _mesh = mesh;
}

Material* MeshRenderer::GetMaterial() const {
    auto& assets = AssetManager::Get();
    if (Material* mat = assets.ResolveMaterial(_material)) {
        return mat;
    }
    Mesh* meshPtr = assets.GetMeshPtr(_mesh);
    return meshPtr ? assets.ResolveMaterial(meshPtr->GetMaterial()) : nullptr;
}

void MeshRenderer::SetMaterial(MaterialHandle material) {
    _material = material;
}

void MeshRenderer::DrawImGui() {
    if (auto* mat = GetMaterial()) mat->DrawImGui();
}