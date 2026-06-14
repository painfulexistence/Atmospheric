#include "sprite_component.hpp"
#include "application.hpp"
#include "batch_renderer_2d.hpp"
#include "canvas_drawable.hpp"
#include "game_object.hpp"
#include "graphics_server.hpp"
#include "renderer.hpp"

SpriteComponent::SpriteComponent(GameObject* gameObject, const SpriteProps& props) : CanvasDrawable(gameObject) {
    _size = props.size;
    _color = props.color;
    _pivot = props.pivot;
    _textureID = props.textureID;
    _layer = props.layer;
    _uvMin = glm::vec2(0.0f, 0.0f);
    _uvMax = glm::vec2(1.0f, 1.0f);
    _flipX = props.flipX;
    _flipY = props.flipY;
    _zOrder = props.zOrder;
}

std::string SpriteComponent::GetName() const {
    return std::string("SpriteComponent");
}

void SpriteComponent::OnAttach() {
    gameObject->GetApp()->GetGraphicsServer()->RegisterCanvasDrawable(this);
}

void SpriteComponent::OnDetach() {
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
    int sortKey = (int)_layer * 1000 + _zOrder;
    renderer->DrawQuad(transform, _textureID, uvs, _color, sortKey);
}
