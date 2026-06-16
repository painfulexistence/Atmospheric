#include "mesh_component.hpp"
#include "application.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "imgui.h"

MeshComponent::MeshComponent(GameObject* gameObject, Mesh* mesh) {
    this->_mesh = mesh;
}

MeshComponent::~MeshComponent() {
}

std::string MeshComponent::GetName() const {
    return std::string("Drawable");
}

void MeshComponent::OnAttach() {
    if (gameObject->GetApp()->GetGraphicsServer()) {
        gameObject->GetApp()->GetGraphicsServer()->RegisterMesh(this);
    }
}

void MeshComponent::OnDetach() {
}

Mesh* MeshComponent::GetMesh() const {
    return _mesh;
}

void MeshComponent::SetMesh(Mesh* mesh) {
    _mesh = mesh;
}

Material* MeshComponent::GetMaterial() const {
    if (_material) {
        return _material;
    } else {
        return _mesh->GetMaterial();
    }
}

void MeshComponent::SetMaterial(Material* material) {
    _material = material;
}

void MeshComponent::DrawImGui() {
    auto* mat = GetMaterial();
    auto& assetManager = AssetManager::Get();
    int textureCount = (int)assetManager.GetTextures().size();
    ImGui::SliderInt("Base map ID",      &mat->baseMap,       -1, textureCount - 1);
    ImGui::SliderInt("Normal map ID",    &mat->normalMap,     -1, textureCount - 1);
    ImGui::SliderInt("AO map ID",        &mat->aoMap,         -1, textureCount - 1);
    ImGui::SliderInt("Roughness map ID", &mat->roughnessMap,  -1, textureCount - 1);
    ImGui::SliderInt("Metallic map ID",  &mat->metallicMap,   -1, textureCount - 1);
    ImGui::SliderInt("Height map ID",    &mat->heightMap,     -1, textureCount - 1);
    ImGui::ColorEdit3("Diffuse",  &mat->diffuse.r);
    ImGui::ColorEdit3("Specular", &mat->specular.r);
    ImGui::ColorEdit3("Ambient",  &mat->ambient.r);
    ImGui::DragFloat("Shininess", &mat->shininess, 0.0f, 1.0f);
    ImGui::Checkbox("Cull face enabled", &mat->cullFaceEnabled);
}