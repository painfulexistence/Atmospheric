#include "water_component.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "mesh_component.hpp"

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
    };
}
