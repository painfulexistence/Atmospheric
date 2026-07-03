#include "terrain_mesh_component.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "height_field.hpp"
#include "height_field_collider_component.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "mesh_component.hpp"
#include "imgui.h"
#include <algorithm>

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
    terrainMat->worldSize          = props.worldSize;

    // Copy base material props if caller provided a material.
    if (props.material) {
        terrainMat->diffuse   = props.material->diffuse;
        terrainMat->specular  = props.material->specular;
        terrainMat->ambient   = props.material->ambient;
        terrainMat->shininess = props.material->shininess;
        terrainMat->baseMap   = props.material->baseMap;
        terrainMat->normalMap = props.material->normalMap;
        terrainMat->aoMap     = props.material->aoMap;
    }

    // Load the optional high-fidelity surface maps (path fields win over any
    // handles copied from props.material).
    auto loadTex = [&am](const std::string& p) {
        return p.empty() ? TextureHandle{} : am.CreateTexture(p);
    };
    if (!props.colorMapPath.empty())  terrainMat->baseMap   = loadTex(props.colorMapPath);
    if (!props.normalMapPath.empty()) terrainMat->normalMap = loadTex(props.normalMapPath);
    if (!props.aoMapPath.empty())     terrainMat->aoMap     = loadTex(props.aoMapPath);
    if (!props.splatMapPath.empty())  terrainMat->splatMap  = loadTex(props.splatMapPath);
    terrainMat->layerCount = std::min((int)props.layers.size(), TerrainMaterial::MAX_LAYERS);
    for (int i = 0; i < terrainMat->layerCount; ++i) {
        terrainMat->layers[i].albedoMap = loadTex(props.layers[i].albedoPath);
        terrainMat->layers[i].normalMap = loadTex(props.layers[i].normalPath);
        terrainMat->layers[i].tiling    = props.layers[i].tiling;
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

    if (auto* noise = dynamic_cast<NoiseHeightField*>(_heightField.get()))
        _appliedParams = noise->Params();
}

void TerrainMeshComponent::DrawImGui() {
    if (_material) {
        ImGui::DragFloat("Height Scale", &_material->heightScale, 0.5f, 0.0f, 256.0f);
        ImGui::DragFloat("Tessellation", &_material->tessellationFactor, 0.5f, 1.0f, 64.0f);
        // Fallback height-palette selection (only visible without color/detail maps).
        static const char* paletteNames[] = {
            "1 - Warm Pink/Gold (default)", "2 - Cool Blue/Purple", "3 - Earthy Green",
            "4 - Forest",                   "5 - Soft Cool",        "6 - Vivid Mint/Coral"
        };
        ImGui::Combo("Palette", &_material->paletteIndex, paletteNames, 6);
        if (_material->layerCount > 0) {
            ImGui::SeparatorText("Layers");
            for (int i = 0; i < _material->layerCount; ++i) {
                ImGui::PushID(i);
                ImGui::DragFloat("Tiling", &_material->layers[i].tiling, 0.5f, 1.0f, 512.0f);
                ImGui::PopID();
            }
        }
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

    // Auto-apply edits, but only once the user releases the drag / leaves the
    // field — regenerating 256x256 FBm on every changed frame would stutter.
    if (p != _appliedParams && !ImGui::IsAnyItemActive()) {
        noise->Regenerate();
        AssetManager::Get().UpdateHeightmapTexture(
            _heightMap, noise->Grid(), noise->Width(), noise->Depth());
        if (auto* collider = gameObject->GetComponent<HeightFieldColliderComponent>())
            collider->SyncFromHeightField();
        _appliedParams = p;
    }
}
