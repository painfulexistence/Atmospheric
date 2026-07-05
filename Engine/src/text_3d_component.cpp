#include "text_3d_component.hpp"
#include "application.hpp"
#include "console_subsystem.hpp"
#include "game_object.hpp"
#include "graphics_subsystem.hpp"
#include "imgui.h"

Text3DComponent::Text3DComponent(GameObject* gameObject, const Text3DProps& props) {
    this->gameObject = gameObject;
    _text = props.text;
    _font = props.font;
    _fontSize = props.fontSize;
    _offset = props.offset;
    _color = props.color;
}

std::string Text3DComponent::GetName() const {
    return std::string("Text3DComponent");
}

void Text3DComponent::DrawImGui() {
    std::string text = GetText();
    char buffer[256];
    strncpy(buffer, text.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    if (ImGui::InputText("Text", buffer, sizeof(buffer))) SetText(std::string(buffer));
    ImGui::Text("Font ID: %d", GetFont().id);
    float fontSize = GetFontSize();
    if (ImGui::DragFloat("Font Size", &fontSize, 0.5f, 1.0f, 200.0f)) SetFontSize(fontSize);
    ImGui::DragFloat3("Offset", &_offset.x, 0.05f);
    glm::vec4 color = GetColor();
    if (ImGui::ColorEdit4("Color", &color.r)) SetColor(color);
}

void Text3DComponent::OnTick(float dt) {
    if (_text.empty()) return;
    if (!gameObject->isActive) return;

    auto* graphics = GraphicsSubsystem::Get();
    if (!graphics) return;

    FontHandle fontID = _font == 0 ? graphics->GetOrCreateDefaultFont() : _font;

    float finalScale = _fontSize / graphics->GetFontBaseSize(fontID);

    glm::vec3 worldPos = gameObject->GetPosition() + _offset;
    graphics->DrawText3D(fontID, _text, worldPos, finalScale, _color);
}
