#pragma once
#include "Atmospheric.hpp"
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Parallax background
// ─────────────────────────────────────────────────────────────────────────────

namespace axex {

// A single horizontally-scrolling background layer.
//
// On attach it spawns two seamless tiles (each `worldSize` wide) and, every
// tick, scrolls them left at `speed` px/s, wrapping around so the layer repeats
// forever. Attach several of these (with different textures / speeds / zOrders)
// to one "background" object to build a multi-layer parallax effect.
class ParallaxLayerComponent : public Component {
public:
    ParallaxLayerComponent(GameObject* go, int textureID, float worldSize, float speed, int zOrder)
        : _textureID(textureID), _worldSize(worldSize), _speed(speed), _zOrder(zOrder) {
    }

    std::string GetName() const override {
        return "ParallaxLayerComponent";
    }

    void OnAttach() override {
        Application* app = gameObject->GetApp();
        for (int i = 0; i < 2; i++) {
            _tiles[i] = app->CreateGameObject(glm::vec2(i * _worldSize, 0.0f));
            _tiles[i]->AddComponent<SpriteComponent>(SpriteProps{
              .size = glm::vec2(_worldSize, _worldSize),
              .pivot = glm::vec2(0.0f, 0.0f),
              .color = glm::vec4(1.0f),
              .textureID = _textureID,
              .layer = CanvasLayer::LAYER_WORLD_2D,
              .flipY = true,
              .zOrder = _zOrder,
            });
        }
    }

    void OnTick(float dt) override {
        _t += dt;
        // Scroll left; keep the leading tile's X in (-worldSize, 0].
        float xA = std::fmod(-_speed * _t, _worldSize);
        if (xA > 0.0f) xA -= _worldSize;
        if (_tiles[0]) _tiles[0]->SetPosition(glm::vec3(xA, 0.0f, 0.0f));
        if (_tiles[1]) _tiles[1]->SetPosition(glm::vec3(xA + _worldSize, 0.0f, 0.0f));
    }

private:
    int _textureID;
    float _worldSize;
    float _speed;
    int _zOrder;
    GameObject* _tiles[2] = { nullptr, nullptr };
    float _t = 0.0f;
};

}// namespace axex
