#pragma once
#include "Atmospheric.hpp"
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Label components
//
// Thin wrappers around the immediate-mode text API (GraphicsServer::DrawText /
// DrawText3D). They turn "draw this text every frame" boilerplate that would
// otherwise live in Application::OnUpdate into a reusable attachable behaviour.
// ─────────────────────────────────────────────────────────────────────────────

namespace axex {

// Draws a billboarded 3D text label that follows the owning object in world space.
class WorldLabelComponent : public Component {
public:
    WorldLabelComponent(
      GameObject* go,
      FontID font,
      std::string text,
      glm::vec3 offset = glm::vec3(0.0f, 1.2f, 0.0f),
      float scale = 0.5f,
      glm::vec4 color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f)
    )
        : _font(font), _text(std::move(text)), _offset(offset), _scale(scale), _color(color) {
    }

    std::string GetName() const override {
        return "WorldLabelComponent";
    }

    void OnTick(float dt) override {
        GraphicsServer::Get()->DrawText3D(_font, _text, gameObject->GetPosition() + _offset, _scale, _color);
    }

    void SetText(std::string text) {
        _text = std::move(text);
    }

private:
    FontID _font;
    std::string _text;
    glm::vec3 _offset;
    float _scale;
    glm::vec4 _color;
};

// Draws a fixed screen-space text label every frame (immediate-mode HUD text).
class ScreenLabelComponent : public Component {
public:
    ScreenLabelComponent(
      GameObject* go,
      FontID font,
      std::string text,
      glm::vec2 position,
      float scale = 1.0f,
      glm::vec4 color = glm::vec4(1.0f)
    )
        : _font(font), _text(std::move(text)), _position(position), _scale(scale), _color(color) {
    }

    std::string GetName() const override {
        return "ScreenLabelComponent";
    }

    void OnTick(float dt) override {
        GraphicsServer::Get()->DrawText(_font, _text, _position.x, _position.y, _scale, _color);
    }

    void SetText(std::string text) {
        _text = std::move(text);
    }

private:
    FontID _font;
    std::string _text;
    glm::vec2 _position;
    float _scale;
    glm::vec4 _color;
};

}// namespace axex
