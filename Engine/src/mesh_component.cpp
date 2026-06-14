#include "mesh_component.hpp"
#include "application.hpp"
#include "game_object.hpp"
#include "material.hpp"
#include "mesh.hpp"

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