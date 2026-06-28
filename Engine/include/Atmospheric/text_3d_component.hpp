#pragma once
#include "component.hpp"
#include "font_manager.hpp"
#include "globals.hpp"
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>

struct Text3DProps {
    std::string text = "";
    FontHandle font = 0;                            // Pre-loaded font ID (0 will fallback to default)
    float fontSize = 24.0f;                         // Desired font size (divided by font base size = render scale)
    glm::vec3 offset = glm::vec3(0.0f, 1.2f, 0.0f); // World space offset
    glm::vec4 color = glm::vec4(1.0f);
};

class Text3DComponent : public Component {
public:
    Text3DComponent(GameObject* gameObject, const Text3DProps& props);

    std::string GetName() const override;

    void OnAttach() override {}
    void OnDetach() override {}
    void DrawImGui() override;

    void OnTick(float dt) override;
    bool CanTick() const override { return true; }

    // Getters
    const std::string& GetText() const { return _text; }
    FontHandle GetFont() const { return _font; }
    float GetFontSize() const { return _fontSize; }
    glm::vec3 GetOffset() const { return _offset; }
    glm::vec4 GetColor() const { return _color; }

    // Setters
    void SetText(const std::string& text) { _text = text; }
    void SetFont(FontHandle font) { _font = font; }
    void SetFontSize(float fontSize) { _fontSize = fontSize; }
    void SetOffset(const glm::vec3& offset) { _offset = offset; }
    void SetColor(const glm::vec4& color) { _color = color; }

private:
    std::string _text;
    FontHandle _font = 0;
    float _fontSize = 24.0f;
    glm::vec3 _offset;
    glm::vec4 _color;
};
