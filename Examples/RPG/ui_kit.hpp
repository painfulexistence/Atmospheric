#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Tiny retained-mode Canvas kit for the RPG's *world-coupled* battle visuals.
//
// The battle backdrop and the combatant sprites live in the engine's CanvasPass
// (SpriteComponent), not RmlUi: they share the game's pixel/render space, need
// per-frame tint/shake, and the actors animate from a FlipbookComponent's UVs —
// all things a DOM/CSS layer is the wrong tool for. Everything *textual*
// (menus, log, stats, dialogue) goes through UIPageComponent instead; see the
// *_ui_page.hpp headers.
//
// These are lightweight handles: Build() spawns the backing GameObject once,
// then you only push new state in (SetCenter / SetColor / SetUVs / SetActive).
// ─────────────────────────────────────────────────────────────────────────────
#include "Atmospheric.hpp"
#include <glm/glm.hpp>

namespace rpgui {

// Battle visuals sit on the top layer the 2D CanvasPass still draws (layers
// >= LAYER_UI_BACK are the RmlUi UIPass, which composites on top — so the RmlUi
// battle menus naturally render over these sprites). zOrder stacks within it.
inline constexpr CanvasLayer kLayer = CanvasLayer::LAYER_WORLD_2D;

enum ZBand : int {
    Z_BACKDROP = 0,
    Z_SCANLINE = 1,
    Z_ACTOR = 10,
};

// ── Quad ─────────────────────────────────────────────────────────────────────
// A flat tinted rectangle (colored quad) backed by a 1×1 white texture; used for
// the battle backdrop and scanlines. Positioned by its top-left corner.
class Quad {
public:
    void Build(Application* app, TextureHandle white, glm::vec2 topLeft, glm::vec2 size, glm::vec4 color, int z) {
        _go = app->CreateGameObject(topLeft);
        _sprite = static_cast<SpriteComponent*>(_go->AddComponent<SpriteComponent>(SpriteProps{
            .size = size,
            .pivot = glm::vec2(0.0f, 0.0f),
            .color = color,
            .texture = white,
            .layer = kLayer,
            .zOrder = z,
        }));
    }
    void SetColor(glm::vec4 c) {
        if (_sprite) _sprite->SetColor(c);
    }
    void SetActive(bool v) {
        if (_go) _go->SetActive(v);
    }

private:
    GameObject* _go = nullptr;
    SpriteComponent* _sprite = nullptr;
};

// ── Sprite ───────────────────────────────────────────────────────────────────
// A textured, UV-addressable quad for combatant actors. The owner pushes screen
// center, tint and the current UV rect (e.g. from a FlipbookComponent) each
// frame. Positioned by its center (pivot 0.5, 0.5).
class Sprite {
public:
    void Build(Application* app, TextureHandle tex, glm::vec2 center, glm::vec2 size, int z = Z_ACTOR) {
        _go = app->CreateGameObject(center);
        _sprite = static_cast<SpriteComponent*>(_go->AddComponent<SpriteComponent>(SpriteProps{
            .size = size,
            .pivot = glm::vec2(0.5f, 0.5f),
            .color = glm::vec4(1.0f),
            .texture = tex,
            .layer = kLayer,
            .zOrder = z,
        }));
    }
    void SetCenter(glm::vec2 c) {
        if (_go) _go->SetPosition(glm::vec3(c, 0.0f));
    }
    void SetColor(glm::vec4 c) {
        if (_sprite) _sprite->SetColor(c);
    }
    void SetUVs(glm::vec2 uvMin, glm::vec2 uvMax) {
        if (_sprite) _sprite->SetUVs(uvMin, uvMax);
    }
    void SetActive(bool v) {
        if (_go) _go->SetActive(v);
    }

private:
    GameObject* _go = nullptr;
    SpriteComponent* _sprite = nullptr;
};

}// namespace rpgui
