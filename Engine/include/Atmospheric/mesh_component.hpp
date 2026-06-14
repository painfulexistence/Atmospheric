#pragma once
#include "component.hpp"
#include "globals.hpp"

class GameObject;

class Mesh;

class Material;

class MeshComponent : public Component {
public:
    MeshComponent(GameObject* gameObject, Mesh* mesh);

    ~MeshComponent();

    std::string GetName() const override;

    void OnAttach() override;
    void OnDetach() override;

    Mesh* GetMesh() const;

    void SetMesh(Mesh* mesh);

    Material* GetMaterial() const;

    void SetMaterial(Material* material);

private:
    Mesh* _mesh = nullptr;
    Material* _material = nullptr;
};