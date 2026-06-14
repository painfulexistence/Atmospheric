#pragma once
#include "Atmospheric.hpp"
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Visual components
//
// Behaviours that animate the appearance of an object's renderable component
// (SpriteComponent / ShapeRendererComponent) rather than its transform.
// ─────────────────────────────────────────────────────────────────────────────

namespace axex {

// Pulses the alpha of the object's SpriteComponent between [minAlpha, maxAlpha].
class SpritePulseComponent : public Component {
public:
    SpritePulseComponent(GameObject* go, float minAlpha, float maxAlpha, float frequency, float phase = 0.0f)
        : _min(minAlpha), _max(maxAlpha), _frequency(frequency), _phase(phase) {
    }

    std::string GetName() const override {
        return "SpritePulseComponent";
    }

    void OnTick(float dt) override {
        _t += dt;
        auto* sprite = gameObject->GetComponent<SpriteComponent>();
        if (!sprite) return;

        float k = 0.5f + 0.5f * std::sin(_t * _frequency + _phase);// 0..1
        glm::vec4 color = sprite->GetColor();
        color.a = _min + (_max - _min) * k;
        sprite->SetColor(color);
    }

private:
    float _min;
    float _max;
    float _frequency;
    float _phase;
    float _t = 0.0f;
};

// Eases the object's ShapeRendererComponent colour back toward the colour it had
// when the component was attached. Combine with an external "flash" (e.g. a
// collision callback that sets the colour to white) for a hit-feedback effect.
class ColorRestoreComponent : public Component {
public:
    ColorRestoreComponent(GameObject* go, float speed = 2.0f) : _speed(speed) {
    }

    std::string GetName() const override {
        return "ColorRestoreComponent";
    }

    void OnAttach() override {
        if (auto* shape = gameObject->GetComponent<ShapeRendererComponent>()) {
            _base = shape->GetColor();
        }
    }

    void OnTick(float dt) override {
        auto* shape = gameObject->GetComponent<ShapeRendererComponent>();
        if (!shape) return;

        float k = std::clamp(dt * _speed, 0.0f, 1.0f);
        glm::vec4 color = glm::mix(shape->GetColor(), _base, k);
        color.a = _base.a;
        shape->SetColor(color);
    }

    void SetBaseColor(glm::vec4 color) {
        _base = color;
    }

private:
    float _speed;
    glm::vec4 _base = glm::vec4(1.0f);
};

}// namespace axex
