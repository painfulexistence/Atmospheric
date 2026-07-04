#include "water_component.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "mesh_component.hpp"
#include "imgui.h"

WaterComponent::WaterComponent(GameObject* owner, const WaterProps& props)
    : _props(props) {
    gameObject = owner;

    auto& am = AssetManager::Get();
    _mesh = am.CreatePlaneMeshSubdivided(
        "Water_" + owner->GetName(),
        props.width, props.depth, props.subdivisions
    );

    _material = am.CreateWaterMaterial();
    if (Mesh* meshPtr = am.GetMeshPtr(_mesh)) meshPtr->SetMaterial(am.GetMaterialHandle(_material));

    owner->AddComponent<MeshComponent>(_mesh);
}

void WaterComponent::OnTick(float /*dt*/) {
    if (auto* wm = dynamic_cast<WaterMaterial*>(_material))
        wm->waterLine = gameObject->GetPosition().y;
}

void WaterComponent::DrawImGui() {
    auto* wm = dynamic_cast<WaterMaterial*>(_material);
    if (!wm) return;
    ImGui::DragFloat("Wave Strength",  &wm->waveStrength,    0.001f, 0.0f,  2.0f);
    ImGui::DragFloat("Wave Speed",     &wm->waveSpeed,       0.01f,  0.0f, 10.0f);
    ImGui::DragFloat("Fog Density",    &wm->waterFogDensity, 0.000001f, 0.0f, 0.001f, "%.6f");
    ImGui::DragFloat("Beer Coef",      &wm->beerCoef,        0.001f, 0.0f,  1.0f);
    ImGui::ColorEdit3("Deep Color",    &wm->deepColor.x);
    ImGui::ColorEdit3("Shallow Color", &wm->shallowColor.x);
}

void WaterComponent::OnAttach() {
    float line = (_props.waterLine > -1e29f)
        ? _props.waterLine
        : gameObject->GetPosition().y;

    if (auto* wm = dynamic_cast<WaterMaterial*>(_material)) {
        wm->waterLine       = line;
        wm->waveStrength    = _props.waveStrength;
        wm->waveSpeed       = _props.waveSpeed;
        wm->waterFogDensity = _props.fogDensity;
        wm->deepColor       = _props.deepColor;
        wm->shallowColor    = _props.shallowColor;
        wm->beerCoef        = _props.beerCoef;
    }
}
