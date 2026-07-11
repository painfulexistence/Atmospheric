#include "terrain_mesh_component.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "height_field.hpp"
#include "height_field_collider_component.hpp"
#include "imgui.h"
#include "material.hpp"
#include "mesh.hpp"
#include "mesh_renderer.hpp"
#include <algorithm>

TerrainMeshComponent::TerrainMeshComponent(
    GameObject* owner,
    GraphicsSubsystem* /*graphics*/,
    const std::shared_ptr<HeightField>& heightField,
    const TerrainMeshProps& props
)
  : _heightField(heightField) {
    gameObject = owner;

    auto& am = AssetManager::Get();
    _mesh = am.CreateTerrainMesh("Terrain_" + owner->GetName(), props.worldSize, props.resolution);
    Mesh* meshPtr = am.GetMeshPtr(_mesh);
    if (meshPtr) {
        // The grid mesh is flat; displacement happens in the shader. Frustum
        // culling only sees this bounding box, so it must span the displaced
        // range or the terrain vanishes when the flat base leaves the frustum.
        const float half = 0.5f * props.worldSize;
        const float top = std::max(props.heightScale, 0.5f);
        meshPtr->SetBoundingBox(
            { { glm::vec3(half, top, half),
                glm::vec3(-half, top, half),
                glm::vec3(-half, -0.5f, half),
                glm::vec3(half, -0.5f, half),
                glm::vec3(half, top, -half),
                glm::vec3(-half, top, -half),
                glm::vec3(-half, -0.5f, -half),
                glm::vec3(half, -0.5f, -half) } }
        );
    }

    TerrainMaterial* terrainMat = am.CreateTerrainMaterial();
    terrainMat->heightScale = props.heightScale;
    terrainMat->tessellationFactor = props.tessellationFactor;
    terrainMat->worldSize = props.worldSize;

    // Copy base material props if caller provided a material.
    if (props.material) {
        terrainMat->diffuse = props.material->diffuse;
        terrainMat->specular = props.material->specular;
        terrainMat->ambient = props.material->ambient;
        terrainMat->shininess = props.material->shininess;
        terrainMat->baseMap = props.material->baseMap;
        terrainMat->normalMap = props.material->normalMap;
        terrainMat->aoMap = props.material->aoMap;
    }

    // Load the optional high-fidelity surface maps (path fields win over any
    // handles copied from props.material).
    auto loadTex = [&am](const std::string& p) { return p.empty() ? TextureHandle{} : am.CreateTexture(p); };
    if (!props.colorMapPath.empty()) terrainMat->baseMap = loadTex(props.colorMapPath);
    if (!props.normalMapPath.empty()) terrainMat->normalMap = loadTex(props.normalMapPath);
    if (!props.aoMapPath.empty()) terrainMat->aoMap = loadTex(props.aoMapPath);
    if (!props.splatMapPath.empty()) terrainMat->splatMap = loadTex(props.splatMapPath);
    terrainMat->layerCount = std::min(static_cast<int>(props.layers.size()), TerrainMaterial::MAX_LAYERS);
    for (int i = 0; i < terrainMat->layerCount; ++i) {
        terrainMat->layers[i].albedoMap = loadTex(props.layers[i].albedoPath);
        terrainMat->layers[i].normalMap = loadTex(props.layers[i].normalPath);
        terrainMat->layers[i].tiling = props.layers[i].tiling;
    }

    // Always bake the height grid to a GPU texture so callers don't need to
    // manually wire up mat->heightMap via LoadScene.
    {
        TextureHandle hmap = am.CreateHeightmapTexture(
            "hm_" + owner->GetName(), heightField->Grid(), heightField->Width(), heightField->Depth()
        );
        terrainMat->heightMap = hmap;
        _heightMap = hmap;
    }

    _material = terrainMat;
    if (meshPtr) meshPtr->SetMaterial(am.GetMaterialHandle(terrainMat));
    owner->AddComponent<MeshRenderer>(_mesh);

    if (auto* noise = dynamic_cast<NoiseHeightField*>(_heightField.get())) _appliedParams = noise->Params();
}

void TerrainMeshComponent::DrawImGui() {
    if (_material) _material->DrawImGui();

    auto* noise = dynamic_cast<NoiseHeightField*>(_heightField.get());
    if (!noise) return;

    auto& p = noise->Params();
    ImGui::SeparatorText("Noise");
    ImGui::InputInt("Seed", &p.seed);
    ImGui::DragFloat("Frequency", &p.frequency, 0.0005f, 0.0001f, 0.1f, "%.4f");
    ImGui::DragInt("Octaves", &p.octaves, 1, 1, 12);
    ImGui::DragFloat("Lacunarity", &p.lacunarity, 0.05f, 1.0f, 4.0f);
    ImGui::DragFloat("Gain", &p.gain, 0.01f, 0.0f, 1.0f);

    // Auto-apply edits, but only once the user releases the drag / leaves the
    // field — regenerating 256x256 FBm on every changed frame would stutter.
    if (p != _appliedParams && !ImGui::IsAnyItemActive()) {
        noise->Regenerate();
        AssetManager::Get().UpdateHeightmapTexture(_heightMap, noise->Grid(), noise->Width(), noise->Depth());
        if (auto* collider = gameObject->GetComponent<HeightFieldColliderComponent>()) collider->SyncFromHeightField();
        _appliedParams = p;
    }
}
