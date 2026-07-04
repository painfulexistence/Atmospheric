#pragma once
#include "Atmospheric.hpp"
#include <cmath>

class RotatorComponent : public Component {
    glm::vec3 _angVel;

public:
    RotatorComponent(GameObject* go, glm::vec3 angVel) : _angVel(angVel) {
        gameObject = go;
    }
    std::string GetName() const override {
        return "RotatorComponent";
    }
    void OnTick(float dt) override {
        gameObject->SetRotation(gameObject->GetRotation() + _angVel * dt);
    }
};

class OscillatorComponent : public Component {
    glm::vec3 _axis, _base;
    float _amp, _freq, _phase, _t = 0;

public:
    OscillatorComponent(GameObject* go, glm::vec3 axis, float amp, float freq, float phase = 0)
      : _axis(axis), _amp(amp), _freq(freq), _phase(phase) {
        gameObject = go;
    }
    std::string GetName() const override {
        return "OscillatorComponent";
    }
    void OnAttach() override {
        _base = gameObject->GetPosition();
    }
    void OnTick(float dt) override {
        _t += dt;
        gameObject->SetPosition(_base + _axis * (std::sin(_t * _freq + _phase) * _amp));
    }
};

class SpritePulseComponent : public Component {
    float _min, _max, _freq, _phase, _t = 0;

public:
    SpritePulseComponent(GameObject* go, float minA, float maxA, float freq, float phase = 0)
      : _min(minA), _max(maxA), _freq(freq), _phase(phase) {
        gameObject = go;
    }
    std::string GetName() const override {
        return "SpritePulseComponent";
    }
    void OnTick(float dt) override {
        _t += dt;
        auto* s = gameObject->GetComponent<SpriteComponent>();
        if (!s) return;
        float k = 0.5f + 0.5f * std::sin(_t * _freq + _phase);
        glm::vec4 c = s->GetColor();
        c.a = _min + (_max - _min) * k;
        s->SetColor(c);
    }
};
