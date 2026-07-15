#pragma once
#include "component.hpp"
#include "globals.hpp"

class GameObject;

class Mesh;

class Material;

class MeshRendererComponent : public Component {
public:
    MeshRendererComponent(GameObject* gameObject, MeshHandle mesh);

    ~MeshRendererComponent();

    std::string GetName() const override;

    void OnAttach() override;
    void OnDetach() override;
    void DrawImGui() override;

    MeshHandle GetMesh() const;

    void SetMesh(MeshHandle mesh);

    // Resolves the override handle, falling back to the mesh's own material.
    Material* GetMaterial() const;

    // The raw per-instance override handle (INVALID when none is set). The
    // submission path forwards this into RenderCommand::material, where INVALID
    // is resolved to the mesh's own material downstream. Distinct from
    // GetMaterial(), which resolves to a Material* for the inspector.
    MaterialHandle GetMaterialHandle() const {
        return _material;
    }

    void SetMaterial(MaterialHandle material);

private:
    MeshHandle _mesh;
    MaterialHandle _material;
};