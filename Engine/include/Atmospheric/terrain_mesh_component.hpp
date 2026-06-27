#pragma once
#include "component.hpp"
#include <memory>

class GraphicsServer;
class HeightField;
class Material;
class Mesh;

struct TerrainMeshProps {
    float     worldSize          = 1024.0f;
    int       resolution         = 128;
    float     heightScale        = 32.0f;
    float     tessellationFactor = 16.0f;
    Material* material           = nullptr;
};

// Creates a tessellated terrain mesh and wires the height-map texture.
// For ImageHeightField the caller must already have Material::heightMap set
// (loaded via LoadScene / AssetManager).  For NoiseHeightField the component
// bakes the grid to a GPU texture and sets Material::heightMap automatically.
class TerrainMeshComponent : public Component {
public:
    TerrainMeshComponent(
        GameObject*                         owner,
        GraphicsServer*                     graphics,
        const std::shared_ptr<HeightField>& heightField,
        const TerrainMeshProps&             props
    );
    ~TerrainMeshComponent();

    std::string GetName() const override { return "TerrainMesh"; }
    void OnAttach() override {}
    void OnDetach() override {}

    Mesh* GetMesh() const { return _mesh; }

private:
    Mesh*      _mesh         = nullptr;
    Material*  _material     = nullptr;
    bool       _ownsMaterial = false;
};
