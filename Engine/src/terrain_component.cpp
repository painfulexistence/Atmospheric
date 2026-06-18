#include "terrain_component.hpp"
#include "game_object.hpp"
#include "graphics_server.hpp"
#include "height_field.hpp"
#include "height_field_collider_component.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "terrain_mesh_component.hpp"

TerrainComponent::TerrainComponent(
    GameObject*         owner,
    GraphicsServer*     graphics,
    PhysicsServer*      /*physics*/,
    const TerrainProps& props
) {
    gameObject = owner;

    // Resolve height source: path → ImageHeightField, or use explicit field.
    std::shared_ptr<HeightField> hf = props.heightField;
    if (!props.heightmapPath.empty())
        hf = std::make_shared<ImageHeightField>(props.heightmapPath);
    if (!hf)
        throw std::runtime_error("TerrainComponent: no heightmapPath or heightField provided");

    auto* tmc = new TerrainMeshComponent(owner, graphics, hf,
        TerrainMeshProps{
            .worldSize          = props.worldSize,
            .resolution         = props.resolution,
            .heightScale        = props.heightScale,
            .tessellationFactor = props.tessellationFactor,
            .material           = props.material,
        });
    owner->AddComponent(tmc);
    _mesh = tmc->GetMesh();

    if (props.buildCollider) {
        owner->AddComponent(new HeightFieldColliderComponent(owner, hf,
            HeightFieldColliderProps{
                .worldSize   = props.worldSize,
                .heightScale = props.heightScale,
                .minHeight   = props.minHeight,
                .maxHeight   = props.maxHeight,
            }));
    }
}

void TerrainComponent::SetMaterial(Material* material) {
    if (_mesh)
        _mesh->SetMaterial(material);
}
