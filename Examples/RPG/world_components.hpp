#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// World-layer Canvas components for the explore scene. These move the last
// immediate-mode drawing out of the RPG: the tilemap and the 2D lighting overlay
// become CanvasDrawables, so the whole explore scene (tilemap → entity sprites →
// lighting) is batched and layer-sorted by the engine's CanvasPass instead of
// being re-issued by hand every frame.
//
// The example still uses a hand-rolled scrolling camera (RPGGame::_camX/_camY,
// screen-space ortho — there is no engine 2D camera set up), so both components
// take a camera-offset getter and draw in screen space, exactly as the old
// Tilemap2D::Draw / LightingSystem2D::Apply did. A proper engine 2D follow-camera
// would let these draw in world space instead (see docs/RPG_UI_REFACTOR.md).
// ─────────────────────────────────────────────────────────────────────────────
#include "Atmospheric.hpp"
#include <Atmospheric/batch_renderer_2d.hpp>
#include <Atmospheric/canvas_drawable.hpp>
#include <Atmospheric/lighting_2d.hpp>
#include <Atmospheric/tilemap_2d.hpp>
#include <algorithm>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Draws the visible tiles of a Tilemap2D as batched textured quads. Mirrors the
// culling + UV math of Tilemap2D::Draw, but emits into BatchRenderer2D so it is
// sorted with the rest of the canvas instead of appended on top as immediate.
class TilemapComponent : public CanvasDrawable {
public:
    TilemapComponent(
        GameObject* go,
        Tilemap2D* map,
        uint32_t tilesetTex,
        std::function<glm::vec2()> getCam,
        int screenW,
        int screenH,
        CanvasLayer layer = CanvasLayer::LAYER_WORLD_BACK
    )
      : CanvasDrawable(go), _map(map), _tex(tilesetTex), _getCam(std::move(getCam)), _screenW(screenW),
        _screenH(screenH), _layer(layer) {
    }

    std::string GetName() const override {
        return "TilemapComponent";
    }
    void OnAttach() override {
        GraphicsSubsystem::Get()->RegisterCanvasDrawable(this);
    }
    void OnDetach() override {
        if (gameObject && gameObject->GetApp() && GraphicsSubsystem::Get())
            GraphicsSubsystem::Get()->UnregisterCanvasDrawable(this);
    }
    CanvasLayer GetLayer() const override {
        return _layer;
    }
    bool CanTick() const override {
        return false;
    }

    void Draw(BatchRenderer2D* renderer) override {
        if (!gameObject->isActive || !_map) return;
        const Tilemap2DData& d = _map->GetData();
        const glm::vec2 cam = _getCam ? _getCam() : glm::vec2(0.0f);
        const int ts = d.tileSize, cols = d.tilesetCols, rows = d.tilesetRows;
        if (ts <= 0 || cols <= 0 || rows <= 0) return;

        const int startCol = std::max(0, static_cast<int>(cam.x / ts));
        const int startRow = std::max(0, static_cast<int>(cam.y / ts));
        const int endCol = std::min(d.width, startCol + _screenW / ts + 2);
        const int endRow = std::min(d.height, startRow + _screenH / ts + 2);

        for (int row = startRow; row < endRow; row++) {
            for (int col = startCol; col < endCol; col++) {
                int idx = d.tiles[row * d.width + col];
                if (idx < 0) continue;
                float sx = static_cast<float>(col * ts) - cam.x;
                float sy = static_cast<float>(row * ts) - cam.y;
                int tc = idx % cols, tr = idx / cols;
                float uMin = static_cast<float>(tc) / cols, uMax = static_cast<float>(tc + 1) / cols;
                float vMin = static_cast<float>(tr) / rows, vMax = static_cast<float>(tr + 1) / rows;
                glm::vec2 uvs[4] = { { uMin, vMin }, { uMax, vMin }, { uMax, vMax }, { uMin, vMax } };
                glm::mat4 t = glm::translate(glm::mat4(1.0f), glm::vec3(sx + ts * 0.5f, sy + ts * 0.5f, 0.0f));
                t = glm::scale(t, glm::vec3(static_cast<float>(ts), static_cast<float>(ts), 1.0f));
                renderer->DrawQuad(t, _tex, uvs, glm::vec4(1.0f));
            }
        }
    }

private:
    Tilemap2D* _map;
    uint32_t _tex;
    std::function<glm::vec2()> _getCam;
    int _screenW, _screenH;
    CanvasLayer _layer;
};

// Draws the LightingSystem2D overlay (ambient darkness + soft light rings) as
// batched quads. Lights are already in screen space (RPGGame fills them as
// world-minus-camera each frame), so this needs no camera. Sits above the world
// sprites (LAYER_EFFECTS) but below the RmlUi HUD — matching the note on
// LightingSystem2D::Apply.
class Lighting2DComponent : public CanvasDrawable {
public:
    Lighting2DComponent(
        GameObject* go,
        LightingSystem2D* lighting,
        int screenW,
        int screenH,
        CanvasLayer layer = CanvasLayer::LAYER_EFFECTS
    )
      : CanvasDrawable(go), _lighting(lighting), _screenW(screenW), _screenH(screenH), _layer(layer) {
    }

    std::string GetName() const override {
        return "Lighting2DComponent";
    }
    void OnAttach() override {
        GraphicsSubsystem::Get()->RegisterCanvasDrawable(this);
    }
    void OnDetach() override {
        if (gameObject && gameObject->GetApp() && GraphicsSubsystem::Get())
            GraphicsSubsystem::Get()->UnregisterCanvasDrawable(this);
    }
    CanvasLayer GetLayer() const override {
        return _layer;
    }
    bool CanTick() const override {
        return false;
    }

    void Draw(BatchRenderer2D* renderer) override {
        if (!gameObject->isActive || !_lighting) return;
        const LightingSystem2D& L = *_lighting;

        float ambientBrightness = std::max({ L.ambientR * L.ambientA, L.ambientG * L.ambientA, L.ambientB * L.ambientA });
        float darkness = std::max(0.0f, 1.0f - ambientBrightness * 1.5f);
        if (darkness > 0.01f)
            renderer->DrawQuad(
                glm::vec2(_screenW * 0.5f, _screenH * 0.5f),
                glm::vec2(static_cast<float>(_screenW), static_cast<float>(_screenH)),
                glm::vec4(L.ambientR * 0.05f, L.ambientG * 0.05f, L.ambientB * 0.05f, darkness)
            );

        constexpr int rings = 16;
        for (const auto& light : L.lights) {
            float clampedIntensity = std::min(light.intensity, 2.0f);
            for (int k = 0; k < rings; k++) {
                float t = static_cast<float>(k + 1) / rings;
                float rad = light.radius * t;
                float alpha = clampedIntensity * (1.0f - t) * (1.0f - t) * 0.30f;
                if (alpha < 0.003f) continue;
                renderer->DrawQuad(
                    glm::vec2(light.x, light.y), glm::vec2(rad * 2.0f, rad * 2.0f), glm::vec4(light.r, light.g, light.b, alpha)
                );
            }
        }
    }

private:
    LightingSystem2D* _lighting;
    int _screenW, _screenH;
    CanvasLayer _layer;
};
