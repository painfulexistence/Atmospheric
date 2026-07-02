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
    Mesh* meshPtr = am.GetMeshPtr(_mesh);

    TerrainMaterial* terrainMat = am.CreateTerrainMaterial();
    terrainMat->heightScale        = props.heightScale;
    terrainMat->tessellationFactor = props.tessellationFactor;

    // Copy base material props if caller provided a material.
    if (props.material) {
        terrainMat->diffuse   = props.material->diffuse;
        terrainMat->specular  = props.material->specular;
        terrainMat->ambient   = props.material->ambient;
        terrainMat->shininess = props.material->shininess;
    }

    // Always bake the height grid to a GPU texture so callers don't need to
    // manually wire up mat->heightMap via LoadScene.
    {
        TextureHandle hmap = am.CreateHeightmapTexture(
            "hm_" + owner->GetName(),
            heightField->Grid(),
            heightField->Width(),
            heightField->Depth()
        );
        terrainMat->heightMap = hmap;
    }

    if (meshPtr) meshPtr->SetMaterial(terrainMat);
    owner->AddComponent<MeshComponent>(_mesh);
}

TerrainMeshComponent::~TerrainMeshComponent() {
    auto& am = AssetManager::Get();
    if (_ownsMaterial) am.RemoveMaterial(_material);
    am.RemoveMesh(_mesh);
    if (gameObject) am.RemoveTexture("hm_" + gameObject->GetName());
}
