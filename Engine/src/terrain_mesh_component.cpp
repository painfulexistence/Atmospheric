#include "terrain_mesh_component.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "height_field.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "mesh_component.hpp"

TerrainMeshComponent::TerrainMeshComponent(
    GameObject*                         owner,
    GraphicsServer*                     /*graphics*/,
    const std::shared_ptr<HeightField>& heightField,
    const TerrainMeshProps&             props
) {
    gameObject = owner;

    auto& am = AssetManager::Get();
    _mesh = am.CreateTerrainMesh("Terrain_" + owner->GetName(), props.worldSize, props.resolution);

    Material* mat = props.material;
    if (!mat)
        mat = am.CreateMaterial(MaterialProps{});

    mat->heightScale        = props.heightScale;
    mat->tessellationFactor = props.tessellationFactor;

    // For NoiseHeightField, bake the height grid to a GPU texture.
    // For ImageHeightField the caller already set mat->heightMap via LoadScene.
    if (dynamic_cast<const NoiseHeightField*>(heightField.get())) {
        int idx = am.CreateHeightmapTexture(
            "hm_" + owner->GetName(),
            heightField->Grid(),
            heightField->Width(),
            heightField->Depth()
        );
        mat->heightMap = idx;
    }

    _mesh->SetMaterial(mat);
    owner->AddComponent<MeshComponent>(_mesh);
}
