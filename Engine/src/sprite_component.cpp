#include "sprite_component.hpp"
#include "application.hpp"
#include "batch_renderer_2d.hpp"
#include "canvas_drawable.hpp"
#include "game_object.hpp"
#include "graphics_subsystem.hpp"
#include "renderer.hpp"
#include "imgui.h"

SpriteComponent::SpriteComponent(GameObject* gameObject, const SpriteProps& props) : CanvasDrawable(gameObject) {
    _size = props.size;
    _color = props.color;
    _pivot = props.pivot;
    _texture = props.texture;
    _layer = props.layer;
    _uvMin = glm::vec2(0.0f, 0.0f);
    _uvMax = glm::vec2(1.0f, 1.0f);
    _flipX = props.flipX;
    _flipY = props.flipY;
    _zOrder = props.zOrder;
}

std::string SpriteComponent::GetName() const {
    return std::string("Sprite");
}

void SpriteComponent::DrawImGui() {
    glm::vec2 size = GetSize();
    glm::vec2 pivot = GetPivot();
    glm::vec4 color = GetColor();
    int textureVal = GetTexture();
    if (ImGui::DragFloat2("Size",  &size.x,  0.001f, 9999.999f)) SetSize(size);
    if (ImGui::DragFloat2("Pivot", &pivot.x, 0.0f,   1.0f))      SetPivot(pivot);
    if (ImGui::ColorEdit4("Color", &color.r))                     SetColor(color);
    auto* graphics = GraphicsSubsystem::Get();
    int minTex = 0, maxTex = static_cast<int>(graphics->canvasTextures.size() - 1);
    if (ImGui::SliderInt("Texture ID", &textureVal, minTex, maxTex))
        SetTexture(textureVal);
}

void SpriteComponent::OnAttach() {
    GraphicsSubsystem::Get()->RegisterCanvasDrawable(this);
}

void SpriteComponent::OnDetach() {
    if (gameObject && gameObject->GetApp() && GraphicsSubsystem::Get()) {
        GraphicsSubsystem::Get()->UnregisterCanvasDrawable(this);
    }
}

void SpriteComponent::Draw(BatchRenderer2D* renderer) {
    // Use world transform to support hierarchy
    glm::mat4 worldTransform = gameObject->GetTransform();

    // Calculate pivot offset in local space (unscaled)
    glm::vec2 pivotOffset = (glm::vec2(0.5f, 0.5f) - _pivot) * _size;

    // Apply pivot and size
    // Note: World transform already includes node position, rotation, and scale
    glm::mat4 transform = glm::translate(worldTransform, glm::vec3(pivotOffset, 0.0f));
    transform = glm::scale(transform, glm::vec3(_size.x, _size.y, 1.0f));

    // Apply flip by swapping UV coordinates
    float uMin = _flipX ? _uvMax.x : _uvMin.x;
    float uMax = _flipX ? _uvMin.x : _uvMax.x;
    float vMin = _flipY ? _uvMax.y : _uvMin.y;
    float vMax = _flipY ? _uvMin.y : _uvMax.y;

    glm::vec2 uvs[4] = {
        { uMin, vMin },// BL
        { uMax, vMin },// BR
        { uMax, vMax },// TR
        { uMin, vMax }// TL
    };

    // Combine layer and zOrder for sorting (layer * 1000 + zOrder)
    int sortKey = static_cast<int>(_layer) * 1000 + _zOrder;
    renderer->DrawQuad(transform, _texture, uvs, _color, sortKey);
}
