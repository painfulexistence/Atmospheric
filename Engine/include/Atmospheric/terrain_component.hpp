#pragma once
#include "component.hpp"
#include "height_field.hpp"
#include <memory>
#include <string>

class GraphicsServer;
class PhysicsServer;
class Material;
class Mesh;

struct TerrainProps {
    // Provide either heightmapPath (image file) or heightField (explicit source).
    // heightmapPath takes priority if both are set.
    std::string                  heightmapPath;
    std::shared_ptr<HeightField> heightField;

    float     worldSize          = 100.0f;
    int       resolution         = 128;
    float     heightScale        = 32.0f;
    float     tessellationFactor = 16.0f;
    float     minHeight          = -64.0f;
    float     maxHeight          =  64.0f;
    Material* material           = nullptr;

    // Set false to skip physics collider (visual-only terrain).
    bool buildCollider = true;
};

// Convenience wrapper: creates a TerrainMeshComponent and optionally a
// HeightFieldColliderComponent on the owner, using a single TerrainProps.
class TerrainComponent : public Component {
public:
    TerrainComponent(
        GameObject*         owner,
        GraphicsServer*     graphics,
        PhysicsServer*      physics,
        const TerrainProps& props
    );

    std::string GetName() const override { return "Terrain"; }
    void OnAttach() override {}
    void OnDetach() override {}

    Mesh* GetMesh() const { return _mesh; }
    void  SetMaterial(Material* material);

private:
    Mesh* _mesh = nullptr;
};
