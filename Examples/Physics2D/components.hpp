#pragma once
#include "Atmospheric.hpp"
#include "Atmospheric/shape_renderer_component.hpp"
#include <algorithm>

// Eases the ShapeRendererComponent colour back to its original value after
// an external colour-flash (e.g. a collision callback that sets it white).
class ColorRestoreComponent : public Component {
    float _speed;
    glm::vec4 _base = glm::vec4(1.0f);
public:
    ColorRestoreComponent(GameObject* go, float speed = 2.0f) : _speed(speed) { gameObject = go; }
    std::string GetName() const override { return "ColorRestoreComponent"; }
    void OnAttach() override {
        if (auto* s = gameObject->GetComponent<ShapeRendererComponent>()) _base = s->GetColor();
    }
    void OnTick(float dt) override {
        auto* s = gameObject->GetComponent<ShapeRendererComponent>();
        if (!s) return;
        float k = std::clamp(dt * _speed, 0.0f, 1.0f);
        glm::vec4 c = glm::mix(s->GetColor(), _base, k);
        c.a = _base.a;
        s->SetColor(c);
    }
};
