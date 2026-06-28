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
    if (!mat) {
        mat = am.CreateMaterial(MaterialProps{});
        _ownsMaterial = true;
    }
    _material = mat;

    _mesh->terrainData = TerrainShaderData{
        .heightScale        = props.heightScale,
        .tessellationFactor = props.tessellationFactor,
    };

    // Always bake the height grid to a GPU texture so callers don't need to
    // manually wire up mat->heightMap via LoadScene.
    {
        TextureHandle hmap = am.CreateHeightmapTexture(
            "hm_" + owner->GetName(),
            heightField->Grid(),
            heightField->Width(),
            heightField->Depth()
        );
        mat->heightMap = hmap;
    }

    _mesh->SetMaterial(mat);
    owner->AddComponent<MeshComponent>(_mesh);
}

TerrainMeshComponent::~TerrainMeshComponent() {
    auto& am = AssetManager::Get();
    if (_ownsMaterial) am.RemoveMaterial(_material);
    am.RemoveMesh(_mesh);
    if (gameObject) am.RemoveTexture("hm_" + gameObject->GetName());
}
