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

    _material = am.CreateMaterial(MaterialProps{});
    _material->renderQueue    = RenderQueue::Transparent;
    _material->cullFaceEnabled = false;
    _mesh->SetMaterial(_material);

    owner->AddComponent<MeshComponent>(_mesh);
}

void WaterComponent::OnTick(float /*dt*/) {
    if (_mesh && _mesh->waterData)
        _mesh->waterData->waterLine = gameObject->GetPosition().y;
}

void WaterComponent::DrawImGui() {
    if (!_mesh || !_mesh->waterData) return;
    auto& wd = *_mesh->waterData;
    ImGui::DragFloat("Wave Strength",  &wd.waveStrength,    0.001f, 0.0f,  2.0f);
    ImGui::DragFloat("Wave Speed",     &wd.waveSpeed,       0.01f,  0.0f, 10.0f);
    ImGui::DragFloat("Fog Density",    &wd.waterFogDensity, 0.0001f, 0.0f, 0.05f);
    ImGui::DragFloat("Beer Coef",      &wd.beerCoef,        0.001f, 0.0f,  1.0f);
    ImGui::ColorEdit3("Fog Color",     &wd.waterFogColor.x);
    ImGui::ColorEdit3("Deep Color",    &wd.deepColor.x);
    ImGui::ColorEdit3("Shallow Color", &wd.shallowColor.x);
}

void WaterComponent::OnAttach() {
    float line = (_props.waterLine > -1e29f)
        ? _props.waterLine
        : gameObject->GetPosition().y;

    _mesh->waterData = WaterShaderData{
        .waterLine       = line,
        .waveStrength    = _props.waveStrength,
        .waveSpeed       = _props.waveSpeed,
        .waterFogColor   = _props.fogColor,
        .waterFogDensity = _props.fogDensity,
        .deepColor       = _props.deepColor,
        .shallowColor    = _props.shallowColor,
        .beerCoef        = _props.beerCoef,
    };
}
