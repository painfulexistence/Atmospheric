#include "mesh_component.hpp"
#include "application.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "imgui.h"

MeshComponent::MeshComponent(GameObject* gameObject, MeshHandle mesh) {
    this->_mesh = mesh;
}

MeshComponent::~MeshComponent() {
}

std::string MeshComponent::GetName() const {
    return std::string("Drawable");
}

void MeshComponent::OnAttach() {
    if (gameObject->GetApp()->GetGraphicsSubsystem()) {
        gameObject->GetApp()->GetGraphicsSubsystem()->RegisterMesh(this);
    }
}

void MeshComponent::OnDetach() {
    if (gameObject && gameObject->GetApp() && gameObject->GetApp()->GetGraphicsSubsystem()) {
        gameObject->GetApp()->GetGraphicsSubsystem()->UnregisterMesh(this);
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
    auto* mat = GetMaterial();
    auto& assetManager = AssetManager::Get();
    int textureCount = static_cast<int>(assetManager.GetTextures().size());
    int baseMap = mat->baseMap;
    if (ImGui::SliderInt("Base map ID", &baseMap, -1, textureCount - 1)) mat->baseMap = baseMap;
    int normalMap = mat->normalMap;
    if (ImGui::SliderInt("Normal map ID", &normalMap, -1, textureCount - 1)) mat->normalMap = normalMap;
    int aoMap = mat->aoMap;
    if (ImGui::SliderInt("AO map ID", &aoMap, -1, textureCount - 1)) mat->aoMap = aoMap;
    int roughnessMap = mat->roughnessMap;
    if (ImGui::SliderInt("Roughness map ID", &roughnessMap, -1, textureCount - 1)) mat->roughnessMap = roughnessMap;
    int metallicMap = mat->metallicMap;
    if (ImGui::SliderInt("Metallic map ID", &metallicMap, -1, textureCount - 1)) mat->metallicMap = metallicMap;
    int heightMap = mat->heightMap;
    if (ImGui::SliderInt("Height map ID", &heightMap, -1, textureCount - 1)) mat->heightMap = heightMap;
    ImGui::ColorEdit3("Diffuse",  &mat->diffuse.r);
    ImGui::ColorEdit3("Specular", &mat->specular.r);
    ImGui::ColorEdit3("Ambient",  &mat->ambient.r);
    ImGui::DragFloat("Shininess", &mat->shininess, 0.0f, 1.0f);
    ImGui::Checkbox("Cull face enabled", &mat->cullFaceEnabled);
}