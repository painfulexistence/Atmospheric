#pragma once
#include "Atmospheric.hpp"
#include "Atmospheric/shape_renderer_component.hpp"
#include <algorithm>

// Marks a shape as participating in the merge/match system.
class MergeableComponent : public Component {
public:
    int  sides        = 3;
    int  minGroup     = 2;    // minimum connected group size to trigger merge
    bool pendingRemove = false;
    bool launched     = false;

    MergeableComponent(GameObject* go, int sides, int minGroup = 2)
        : sides(sides), minGroup(minGroup) { gameObject = go; }

    std::string GetName() const override { return "MergeableComponent"; }
};

// Eases the ShapeRendererComponent colour toward a target each tick.
// If no target is provided, captures the spawn colour in OnAttach.
// Used to recover from a collision flash (e.g. SetColor to white on contact).
class ColorRestoreComponent : public Component {
    float     _speed;
    glm::vec4 _base{-1.0f};   // sentinel: -1 means "capture on attach"
public:
    ColorRestoreComponent(GameObject* go, float speed = 2.0f,
                          glm::vec4 target = glm::vec4(-1.0f))
        : _speed(speed), _base(target) { gameObject = go; }

    std::string GetName() const override { return "ColorRestoreComponent"; }

    void OnAttach() override {
        if (_base.r < 0.0f)   // no explicit target — use spawn colour
            if (auto* s = gameObject->GetComponent<ShapeRendererComponent>())
                _base = s->GetColor();
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
