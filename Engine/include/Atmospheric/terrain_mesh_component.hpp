#pragma once
#include "component.hpp"
#include "globals.hpp"
#include "height_field.hpp"
#include <memory>
#include <string>
#include <vector>

class GraphicsSubsystem;
class HeightField;
class Material;
class Mesh;
class TerrainMaterial;

// Optional detail layer, loaded from disk (see TerrainMeshProps::layers).
struct TerrainLayerDesc {
    std::string albedoPath;
    std::string normalPath;// optional tangent-space detail normal map
    float tiling = 32.0f;// repeats across the whole terrain
};

struct TerrainMeshProps {
    float worldSize = 1024.0f;
    int resolution = 128;
    float heightScale = 32.0f;
    float tessellationFactor = 16.0f;
    Material* material = nullptr;

    // High-fidelity surface maps (WorldCreator/Gaea exports); all optional.
    // Full-terrain maps sampled with 0-1 UV across the whole terrain:
    std::string colorMapPath;// Gaea "Texture" / WorldCreator color map
    std::string normalMapPath;// exported normal map (else derived from heightmap)
    std::string aoMapPath;// ambient occlusion
    std::string splatMapPath;// RGBA weights for the detail layers below
    std::vector<TerrainLayerDesc> layers;// up to 4 tiled detail layers
};

// Creates a tessellated terrain mesh and wires the height-map texture.
// For ImageHeightField the caller must already have Material::heightMap set
// (loaded via LoadScene / AssetManager).  For NoiseHeightField the component
// bakes the grid to a GPU texture and sets Material::heightMap automatically.
class TerrainMeshComponent : public Component {
public:
    TerrainMeshComponent(
        GameObject* owner,
        GraphicsSubsystem* graphics,
        const std::shared_ptr<HeightField>& heightField,
        const TerrainMeshProps& props
    );
    ~TerrainMeshComponent() = default;

    std::string GetName() const override {
        return "TerrainMesh";
    }
    void OnAttach() override {
    }
    void OnDetach() override {
    }
    // Exposes heightScale/tessellation and, for NoiseHeightField sources, the
    // noise parameters (seed, frequency, ...). Edits regenerate the height
    // grid, the GPU heightmap, and any sibling HeightFieldColliderComponent.
    void DrawImGui() override;

    MeshHandle GetMesh() const {
        return _mesh;
    }
    // Direct access to the terrain surface parameters — assign extra maps
    // (baseMap/normalMap/aoMap/splatMap/layers) after construction if you
    // prefer TextureHandles over the path fields in TerrainMeshProps.
    TerrainMaterial* GetTerrainMaterial() const {
        return _material;
    }

private:
    MeshHandle _mesh;
    std::shared_ptr<HeightField> _heightField;
    TerrainMaterial* _material = nullptr;
    TextureHandle _heightMap;
    NoiseHeightFieldParams _appliedParams;// last params baked to GPU/collider
};
