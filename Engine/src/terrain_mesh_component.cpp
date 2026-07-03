#include "terrain_mesh_component.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "height_field.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "mesh_component.hpp"
#include "imgui.h"

TerrainMeshComponent::TerrainMeshComponent(
    GameObject*                         owner,
    GraphicsServer*                     /*graphics*/,
    const std::shared_ptr<HeightField>& heightField,
    const TerrainMeshProps&             props
) : _heightField(heightField) {
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
        _heightMap = hmap;
    }

    _material = terrainMat;
    if (meshPtr) meshPtr->SetMaterial(terrainMat);
    owner->AddComponent<MeshComponent>(_mesh);
}

void TerrainMeshComponent::DrawImGui() {
    if (_material) {
        ImGui::DragFloat("Height Scale", &_material->heightScale, 0.5f, 0.0f, 256.0f);
        ImGui::DragFloat("Tessellation", &_material->tessellationFactor, 0.5f, 1.0f, 64.0f);
    }

    auto* noise = dynamic_cast<NoiseHeightField*>(_heightField.get());
    if (!noise) return;

    auto& p = noise->Params();
    ImGui::SeparatorText("Noise");
    ImGui::InputInt("Seed",        &p.seed);
    ImGui::DragFloat("Frequency",  &p.frequency, 0.0005f, 0.0001f, 0.1f, "%.4f");
    ImGui::DragInt("Octaves",      &p.octaves, 1, 1, 12);
    ImGui::DragFloat("Lacunarity", &p.lacunarity, 0.05f, 1.0f, 4.0f);
    ImGui::DragFloat("Gain",       &p.gain, 0.01f, 0.0f, 1.0f);
    if (ImGui::Button("Regenerate")) {
        noise->Regenerate();
        AssetManager::Get().UpdateHeightmapTexture(
            _heightMap, noise->Grid(), noise->Width(), noise->Depth());
    }
}
