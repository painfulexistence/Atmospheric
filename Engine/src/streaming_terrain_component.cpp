#include "streaming_terrain_component.hpp"
#include "application.hpp"
#include "camera_component.hpp"
#include "game_object.hpp"
#include "imgui.h"

StreamingTerrainComponent::StreamingTerrainComponent(GameObject* owner, const StreamingTerrainProps& props)
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
    if (!_camera) {
        // Scene-load ordering: if this terrain was instantiated before the
        // camera entity, the main camera wasn't available at OnAttach — retry
        // lazily until it exists.
        if (Application* app = gameObject->GetApp()) _camera = app->GetMainCamera();
        if (!_camera) return;
    }
    const glm::vec3 camPos = _camera->gameObject->GetPosition();
    const glm::mat4 viewProj = _camera->GetProjectionMatrix() * _camera->GetViewMatrix();
    _streamer.Update(camPos, viewProj);
}

void StreamingTerrainComponent::DrawImGui() {
    // The editor (or any host) already wraps this in a CollapsingHeader keyed
    // on the component name — draw content directly, no second header (a
    // same-named inner header collides IDs: Dear ImGui "0 visible items" /
    // duplicate-ID programmer error).
    const TerrainStreamer::Stats& s = _streamer.GetStats();
    ImGui::Text("Tiles %d (visible %d, pending %d)", s.loadedTiles, s.visibleTiles, s.pendingJobs);
    ImGui::Text("Entities %d", s.activeEntities);
    ImGui::Text("Grass %d cells / %d blades", s.grassCells, s.grassBlades);
    ImGui::Text("Cache %d/%d hit", s.cacheHits, s.cacheHits + s.cacheMisses);
    ImGui::Text("Heightmap %zu MB", s.gpuHeightmapBytes / (1024 * 1024));

    // ── Surface ──────────────────────────────────────────────────────────
    // The three shading modes are mutually exclusive — one selector, not
    // overlapping toggles. Palette / LOD tint suspend the detail layers so
    // their flat colours show (see SetColorMode).
    static const char* modeNames[] = { "Textured (layers)", "Palette", "LOD tint" };
    int mode = static_cast<int>(_streamer.GetColorMode());
    if (ImGui::Combo("Surface", &mode, modeNames, 3)) _streamer.SetColorMode(static_cast<TerrainColorMode>(mode));

    // Palette index only bites in Palette mode; grey it out otherwise.
    static const char* paletteNames[] = { "0 - Warm Pink/Gold", "1 - Cool Blue/Purple",    "2 - Earthy Green",
                                          "3 - Forest",         "4 - Soft Cool (default)", "5 - Vivid Mint/Coral" };
    ImGui::BeginDisabled(_streamer.GetColorMode() != TerrainColorMode::Palette);
    int palette = _streamer.GetPalette();
    if (ImGui::Combo("Palette", &palette, paletteNames, 6)) _streamer.SetPalette(palette);
    ImGui::EndDisabled();

    // ── Grass ────────────────────────────────────────────────────────────
    bool grass = _streamer.GetGrassEnabled();
    if (ImGui::Checkbox("Grass", &grass)) _streamer.SetGrassEnabled(grass);
    ImGui::BeginDisabled(!grass);
    float radius = _streamer.GetGrassRadius();
    if (ImGui::SliderFloat("Grass view dist", &radius, 16.0f, 512.0f, "%.0f m")) _streamer.SetGrassRadius(radius);
    // Root/tip tint — tune blades to match the terrain grass layer.
    glm::vec3 root = _streamer.GetGrassRootColor(), tip = _streamer.GetGrassTipColor();
    bool changed = ImGui::ColorEdit3("Grass root", &root.x);
    changed |= ImGui::ColorEdit3("Grass tip", &tip.x);
    if (changed) _streamer.SetGrassColors(root, tip);
    ImGui::EndDisabled();
}
