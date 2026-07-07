#include "streaming_terrain_component.hpp"
#include "application.hpp"
#include "camera_component.hpp"
#include "game_object.hpp"
#include "imgui.h"

StreamingTerrainComponent::StreamingTerrainComponent(GameObject* owner, const TerrainStreamerProps& props)
  : _props(props) {
    gameObject = owner;
}

void StreamingTerrainComponent::OnAttach() {
    Application* app = gameObject->GetApp();
    if (!app) return;
    // The owning GameObject roots every tile/collider/entity the streamer
    // creates, so they inherit its transform and don't pollute the top level.
    _streamer.Init(app, _props, gameObject);
    if (!_camera) _camera = app->GetMainCamera();
}

void StreamingTerrainComponent::OnTick(float /*dt*/) {
    if (!_camera) return;
    const glm::vec3 camPos = _camera->gameObject->GetPosition();
    const glm::mat4 viewProj = _camera->GetProjectionMatrix() * _camera->GetViewMatrix();
    _streamer.Update(camPos, viewProj);
}

void StreamingTerrainComponent::DrawImGui() {
    if (!ImGui::CollapsingHeader(GetName().c_str())) return;
    const TerrainStreamer::Stats& s = _streamer.GetStats();
    ImGui::Text("Tiles %d (visible %d, pending %d)", s.loadedTiles, s.visibleTiles, s.pendingJobs);
    ImGui::Text("Entities %d", s.activeEntities);
    ImGui::Text("Cache %d/%d hit", s.cacheHits, s.cacheHits + s.cacheMisses);
    ImGui::Text("Heightmap %zu MB", s.gpuHeightmapBytes / (1024 * 1024));
    bool tint = _streamer.GetLodTintDebug();
    if (ImGui::Checkbox("LOD tint", &tint)) _streamer.SetLodTintDebug(tint);
}
