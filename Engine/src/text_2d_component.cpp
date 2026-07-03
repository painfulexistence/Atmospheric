#include "text_2d_component.hpp"
#include "application.hpp"
#include "batch_renderer_2d.hpp"
#include "console_subsystem.hpp"
#include "game_object.hpp"
#include "graphics_subsystem.hpp"
#include "imgui.h"

Text2DComponent::Text2DComponent(GameObject* gameObject, const Text2DProps& props) : CanvasDrawable(gameObject) {
    _text = props.text;
    _font = props.font;
    _fontSize = props.fontSize;
    _size = props.size;
    _pivot = props.pivot;
    _color = props.color;
    _hAlign = props.hAlign;
    _vAlign = props.vAlign;
    _layer = props.layer;
    _zOrder = props.zOrder;
}

std::string Text2DComponent::GetName() const {
    return std::string("Text2DComponent");
}

void Text2DComponent::DrawImGui() {
    std::string text = GetText();
    char buffer[256];
    strncpy(buffer, text.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    if (ImGui::InputText("Text", buffer, sizeof(buffer))) SetText(std::string(buffer));
    ImGui::Text("Font ID: %d", GetFont().id);
    float fontSize = GetFontSize();
    if (ImGui::DragFloat("Font Size", &fontSize, 1.0f, 1.0f, 200.0f)) SetFontSize(fontSize);
    glm::vec4 color = GetColor();
    if (ImGui::ColorEdit4("Color", &color.r)) SetColor(color);
}

void Text2DComponent::OnAttach() {
    ConsoleSubsystem::Get()->Info(fmt::format("Text2DComponent: Attaching with text='{}'", _text));
    auto* graphics = gameObject->GetApp()->GetGraphicsSubsystem();
    graphics->RegisterCanvasDrawable(this);

    // Resolve font fallback
    if (_font == 0) {
        _font = graphics->GetOrCreateDefaultFont();
    }
}

void Text2DComponent::OnDetach() {
    if (gameObject && gameObject->GetApp() && gameObject->GetApp()->GetGraphicsSubsystem()) {
        gameObject->GetApp()->GetGraphicsSubsystem()->UnregisterCanvasDrawable(this);
    }
}

void Text2DComponent::Draw(BatchRenderer2D* renderer) {
    if (_text.empty()) return;
    if (!gameObject->isActive) return;

    auto* graphics = GraphicsSubsystem::Get();
    if (!graphics) return;

    FontHandle fontID = _font == 0 ? graphics->GetOrCreateDefaultFont() : _font;

    // Derive scale from the font's actual baked base size so that
    // fontSize=32 with a font loaded at 32px gives exactly scale=1.0.
    float scale = _fontSize / graphics->GetFontBaseSize(fontID);

    // Measure the text to determine its actual size
    glm::vec2 textSize = graphics->MeasureText(fontID, _text, scale);

    // Get world transform (includes parent transforms)
    glm::mat4 worldTransform = gameObject->GetTransform();
    glm::vec3 worldPos = glm::vec3(worldTransform[3]);

    // Calculate alignment offset within the bounding box
    float alignOffsetX = 0.0f;
    float alignOffsetY = 0.0f;

    switch (_hAlign) {
    case TextHAlignment::Left:
        alignOffsetX = 0.0f;
        break;
    case TextHAlignment::Center:
        alignOffsetX = (_size.x - textSize.x) * 0.5f;
        break;
    case TextHAlignment::Right:
        alignOffsetX = _size.x - textSize.x;
        break;
    }

    switch (_vAlign) {
    case TextVAlignment::Top:
        alignOffsetY = 0.0f;
        break;
    case TextVAlignment::Center:
        alignOffsetY = (_size.y - textSize.y) * 0.5f;
        break;
    case TextVAlignment::Bottom:
        alignOffsetY = _size.y - textSize.y;
        break;
    }

    // Apply pivot offset (pivot is 0,0 = top-left, 1,1 = bottom-right)
    float pivotOffsetX = -_pivot.x * _size.x;
    float pivotOffsetY = -_pivot.y * _size.y;

    // Final text position
    float textX = worldPos.x + pivotOffsetX + alignOffsetX;
    float textY = worldPos.y + pivotOffsetY + alignOffsetY;

    graphics->DrawText(fontID, _text, textX, textY, scale, _color);
}
