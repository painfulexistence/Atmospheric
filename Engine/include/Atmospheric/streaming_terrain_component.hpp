#pragma once
#include "component.hpp"
#include "terrain_streamer.hpp"

class CameraComponent;

// Reusable engine wrapper that makes a streamed open-world terrain a
// first-class scene component. Owns a TerrainStreamer, initialises it on
// attach (the owning GameObject is the tile/collider/entity root), and drives
// it each tick relative to a camera. Mirrors VoxelWorldComponent's
// manager-owning-component pattern.
//
//   auto* go = CreateGameObject(glm::vec3(0.0f));
//   go->AddComponent<StreamingTerrainComponent>(StreamingTerrainProps{
//       .worldSize = 10240.0f, .tileSize = 512.0f, .heightScale = 500.0f, ... });
//
// By default it drives off the application's main camera; call SetCamera() to
// stream relative to a different view. The generator/entity/splat callbacks in
// StreamingTerrainProps stay code-only (std::function), so this component is the
// integration seam for terrain regardless of whether the scene is built in C++
// or (for the scalar props) later declared in scene JSON.
class StreamingTerrainComponent : public Component {
public:
    StreamingTerrainComponent(GameObject* owner, const StreamingTerrainProps& props = {});

    std::string GetName() const override {
        return "StreamingTerrain";
    }
    void OnAttach() override;
    void OnTick(float dt) override;
    void DrawImGui() override;

    // Stream relative to this camera instead of the application's main camera.
    void SetCamera(CameraComponent* camera) {
        _camera = camera;
    }

    // Direct access to the underlying streamer (height queries, LOD-tint debug,
    // stats, live prop inspection).
    TerrainStreamer& Streamer() {
        return _streamer;
    }
    const TerrainStreamer& Streamer() const {
        return _streamer;
    }

    // Convenience passthroughs for the common callers (ground clamp, HUD).
    float GetHeight(float wx, float wz) const {
        return _streamer.GetHeight(wx, wz);
    }
    const TerrainStreamer::Stats& GetStats() const {
        return _streamer.GetStats();
    }
    // World-level height-palette (0-5, wraps) — recolors the whole terrain.
    void SetPalette(int index) {
        _streamer.SetPalette(index);
    }
    int GetPalette() const {
        return _streamer.GetPalette();
    }

private:
    TerrainStreamer _streamer;
    StreamingTerrainProps _props;
    CameraComponent* _camera = nullptr;
};
