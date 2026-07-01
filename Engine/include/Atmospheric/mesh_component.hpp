#pragma once
#include "component.hpp"
#include "globals.hpp"

class GameObject;

class Mesh;

class Material;

class MeshComponent : public Component {
public:
    MeshComponent(GameObject* gameObject, MeshHandle mesh);

    ~MeshComponent();

    std::string GetName() const override;

    void OnAttach() override;
    void OnDetach() override;
    void DrawImGui() override;

    MeshHandle GetMesh() const;

    void SetMesh(MeshHandle mesh);

    Material* GetMaterial() const;

    void SetMaterial(Material* material);

private:
    MeshHandle _mesh;
    Material* _material = nullptr;
};