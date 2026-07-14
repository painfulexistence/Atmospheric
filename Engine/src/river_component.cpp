#include "river_component.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "mesh_component.hpp"
#include "streaming_terrain_component.hpp"
#include "terrain_streamer.hpp"

#include <array>

RiverComponent::RiverComponent(
    GameObject* owner, StreamingTerrainComponent* terrain, std::vector<RiverPolyline> rivers, const RiverProps& props
)
  : _terrain(terrain), _rivers(std::move(rivers)), _props(props) {
    gameObject = owner;
}

void RiverComponent::OnAttach() {
    if (!_terrain) return;
    const StreamingTerrainProps& tp = _terrain->Streamer().Props();
    const float heightScale = tp.heightScale;

    _riverCount = static_cast<int>(_rivers.size());
    if (_rivers.empty()) return;

    // Drape onto the terrain's CURRENT height — which is the carved height the
    // tiles were generated from — so the ribbon sits at the bottom of the
    // incised channel with banks rising on either side.
    StreamingTerrainComponent* terrain = _terrain;
    auto carvedHeight01 = [terrain, heightScale](float wx, float wz) {
        return heightScale > 0.0f ? terrain->GetHeight(wx, wz) / heightScale : 0.0f;
    };

    std::vector<Vertex> verts;
    std::vector<uint16_t> indices;
    BuildRiverMesh(_rivers, carvedHeight01, heightScale, _props.mesh, verts, indices);
    if (verts.empty() || indices.empty()) return;
    _triCount = indices.size() / 3;

    auto& am = AssetManager::Get();
    auto* rm = am.CreateRiverMaterial();
    rm->shallowColor = _props.shallowColor;
    rm->deepColor = _props.deepColor;
    rm->flowSpeed = _props.flowSpeed;
    rm->rippleStrength = _props.rippleStrength;
    rm->glint = _props.glint;
    rm->alpha = _props.alpha;
    rm->fogColor = _props.fogColor;
    rm->fogDensity = (_props.fogDensity >= 0.0f) ? _props.fogDensity : tp.fogDensity;
    _material = rm;

    auto* mesh = new Mesh(MeshType::RIVER);
    mesh->Initialize(verts, indices);
    mesh->SetMaterial(am.GetMaterialHandle(rm));
    // The ribbon spans the whole world; a world-covering AABB keeps it from
    // being frustum-culled (its vertices are already world-space, owner = I).
    const float h = 0.5f * tp.worldSize, top = heightScale + 4.0f;
    mesh->SetBoundingBox(
        { { glm::vec3(-h, top, -h),
            glm::vec3(h, top, -h),
            glm::vec3(h, 0.0f, -h),
            glm::vec3(-h, 0.0f, -h),
            glm::vec3(-h, top, h),
            glm::vec3(h, top, h),
            glm::vec3(h, 0.0f, h),
            glm::vec3(-h, 0.0f, h) } }
    );
    _mesh = am.CreateMesh("rivers_" + gameObject->GetName(), mesh);
    gameObject->AddComponent<MeshComponent>(_mesh);
}
