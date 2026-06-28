#pragma once
#include "Atmospheric.hpp"
#include <cmath>

class RotatorComponent : public Component {
    glm::vec3 _angVel;
public:
    RotatorComponent(GameObject* go, glm::vec3 angVel) : _angVel(angVel) { gameObject = go; }
    std::string GetName() const override { return "RotatorComponent"; }
    void OnTick(float dt) override {
        gameObject->SetRotation(gameObject->GetRotation() + _angVel * dt);
    }
};

class OscillatorComponent : public Component {
    glm::vec3 _axis, _base;
    float _amp, _freq, _phase, _t = 0;
public:
    OscillatorComponent(GameObject* go, glm::vec3 axis, float amp, float freq, float phase = 0)
        : _axis(axis), _amp(amp), _freq(freq), _phase(phase) { gameObject = go; }
    std::string GetName() const override { return "OscillatorComponent"; }
    void OnAttach() override { _base = gameObject->GetPosition(); }
    void OnTick(float dt) override {
        _t += dt;
        gameObject->SetPosition(_base + _axis * (std::sin(_t * _freq + _phase) * _amp));
    }
};

class WorldLabelComponent : public Component {
    FontHandle _font; std::string _text; glm::vec3 _offset; float _scale; glm::vec4 _color;
public:
    WorldLabelComponent(GameObject* go, FontHandle font, std::string text,
                        glm::vec3 offset = {0, 1.2f, 0}, float scale = 0.5f,
                        glm::vec4 color = {1, 1, 0, 1})
        : _font(font), _text(std::move(text)), _offset(offset), _scale(scale), _color(color) {
        gameObject = go;
    }
    std::string GetName() const override { return "WorldLabelComponent"; }
    void OnTick(float) override {
        GraphicsServer::Get()->DrawText3D(_font, _text, gameObject->GetPosition() + _offset, _scale, _color);
    }
};

class ScreenLabelComponent : public Component {
    FontHandle _font; std::string _text; glm::vec2 _pos; float _scale; glm::vec4 _color;
public:
    ScreenLabelComponent(GameObject* go, FontHandle font, std::string text, glm::vec2 pos,
                         float scale = 1.0f, glm::vec4 color = {1, 1, 1, 1})
        : _font(font), _text(std::move(text)), _pos(pos), _scale(scale), _color(color) {
        gameObject = go;
    }
    std::string GetName() const override { return "ScreenLabelComponent"; }
    void OnTick(float) override {
        GraphicsServer::Get()->DrawText(_font, _text, _pos.x, _pos.y, _scale, _color);
    }
};

class SpritePulseComponent : public Component {
    float _min, _max, _freq, _phase, _t = 0;
public:
    SpritePulseComponent(GameObject* go, float minA, float maxA, float freq, float phase = 0)
        : _min(minA), _max(maxA), _freq(freq), _phase(phase) { gameObject = go; }
    std::string GetName() const override { return "SpritePulseComponent"; }
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
