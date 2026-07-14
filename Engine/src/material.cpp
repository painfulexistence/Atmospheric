#include "material.hpp"
#include "asset_manager.hpp"
#include "imgui.h"

void Material::DrawImGui() {
    auto& assets = AssetManager::Get();
    int textureCount = static_cast<int>(assets.GetTextures().size());
    int baseMapID = baseMap;
    if (ImGui::SliderInt("Base map ID", &baseMapID, -1, textureCount - 1)) baseMap = baseMapID;
    int normalMapID = normalMap;
    if (ImGui::SliderInt("Normal map ID", &normalMapID, -1, textureCount - 1)) normalMap = normalMapID;
    int aoMapID = aoMap;
    if (ImGui::SliderInt("AO map ID", &aoMapID, -1, textureCount - 1)) aoMap = aoMapID;
    int roughnessMapID = roughnessMap;
    if (ImGui::SliderInt("Roughness map ID", &roughnessMapID, -1, textureCount - 1)) roughnessMap = roughnessMapID;
    int metallicMapID = metallicMap;
    if (ImGui::SliderInt("Metallic map ID", &metallicMapID, -1, textureCount - 1)) metallicMap = metallicMapID;
    ImGui::ColorEdit3("Diffuse", &diffuse.r);
    static const char* cullNames[] = { "None", "Front", "Back" };
    int cullIdx = static_cast<int>(renderState.cull);
    if (ImGui::Combo("Cull mode", &cullIdx, cullNames, IM_ARRAYSIZE(cullNames))) {
        renderState.cull = static_cast<CullMode>(cullIdx);
    }
}

void PBRMaterial::DrawImGui() {
    Material::DrawImGui();
    ImGui::DragFloat("Roughness factor", &roughnessFactor, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Metallic factor", &metallicFactor, 0.01f, 0.0f, 1.0f);
}

void BlinnPhongMaterial::DrawImGui() {
    Material::DrawImGui();
    ImGui::ColorEdit3("Specular", &specular.r);
    ImGui::ColorEdit3("Ambient", &ambient.r);
    ImGui::DragFloat("Shininess", &shininess, 0.0f, 1.0f);
}

// VoxelMaterial's DrawImGui is defined inline as a no-op — VoxelChunkPass
// ignores the entire base material surface (maps, Phong, cull, polygon), so
// there's nothing meaningful to expose. Palette lives on VoxelWorld and is
// edited from VoxelWorldComponent.

void TerrainMaterial::DrawImGui() {
    Material::DrawImGui();
    int textureCount = static_cast<int>(AssetManager::Get().GetTextures().size());
    int heightMapID = heightMap;
    if (ImGui::SliderInt("Height map ID", &heightMapID, -1, textureCount - 1)) heightMap = heightMapID;
    ImGui::DragFloat("Height Scale", &heightScale, 0.5f, 0.0f, 256.0f);
    ImGui::DragFloat("Tessellation", &tessellationFactor, 0.5f, 1.0f, 64.0f);
    // Fallback height-palette selection (only visible without color/detail maps).
    static const char* gpaletteNames[] = {
        "1 - Warm Pink/Gold (default)", "2 - Cool Blue/Purple", "3 - Earthy Green", "4 - Forest", "5 - Soft Cool",
        "6 - Vivid Mint/Coral"
    };
    ImGui::Combo("Palette", &paletteIndex, gpaletteNames, 6);
    if (layerCount > 0) {
        ImGui::SeparatorText("Layers");
        for (int i = 0; i < layerCount; ++i) {
            ImGui::PushID(i);
            ImGui::DragFloat("Tiling", &layers[i].tiling, 0.5f, 1.0f, 512.0f);
            ImGui::PopID();
        }
    }
}
