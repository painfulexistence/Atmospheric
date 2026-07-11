#pragma once
#include "component.hpp"
#include "globals.hpp"

class GameObject;

class Mesh;

class Material;

class MeshRenderer : public Component {
public:
    MeshRenderer(GameObject* gameObject, MeshHandle mesh);

    ~MeshRenderer();

    std::string GetName() const override;

    void OnAttach() override;
    void OnDetach() override;
    void DrawImGui() override;

    MeshHandle GetMesh() const;

    void SetMesh(MeshHandle mesh);

    // Resolves the override handle, falling back to the mesh's own material.
    Material* GetMaterial() const;

    void SetMaterial(MaterialHandle material);

private:
    MeshHandle _mesh;
    MaterialHandle _material;
};