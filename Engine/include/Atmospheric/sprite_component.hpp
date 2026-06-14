#pragma once
#include "canvas_drawable.hpp"
#include "component.hpp"
#include "globals.hpp"
#include "graphics_server.hpp"
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

struct SpriteProps {
    glm::vec2 size = glm::vec2(100.0f, 100.0f);
    glm::vec2 pivot = glm::vec2(0.5f, 0.5f);
    glm::vec4 color = glm::vec4(1.0f);
    int textureID = -1;
    CanvasLayer layer = CanvasLayer::LAYER_WORLD;// Default to main game layer
    bool flipX = false;
    bool flipY = false;
    int zOrder = 0;// Fine-grained ordering within same layer
};


class SpriteComponent : public CanvasDrawable {
public:
    SpriteComponent(GameObject* gameObject, const SpriteProps& props);
    // virtual ~SpriteComponent() = default;

    std::string GetName() const override;

    void OnAttach() override;
    void OnDetach() override;

    void Draw(BatchRenderer2D* renderer) override;

    bool CanTick() const override {
        return false;
    }

    glm::vec2 GetSize() {
        return _size;
    }
    glm::vec2 GetPivot() {
        return _pivot;
    }
    glm::vec4 GetColor() {
        return _color;
    }
    int GetTextureID() {
        return _textureID;
    }

    void SetSize(const glm::vec2& size) {
        _size = size;
    }
    void SetPivot(const glm::vec2& pivot) {
        _pivot = pivot;
    }
    void SetColor(const glm::vec4& color) {
        _color = color;
    }
    void SetTextureID(int textureID) {
        _textureID = textureID;
    }

    CanvasLayer GetLayer() const override {
        return _layer;
    }
    void SetLayer(CanvasLayer layer) {
        _layer = layer;
    }

    glm::vec2 GetUVMin() const {
        return _uvMin;
    }
    glm::vec2 GetUVMax() const {
        return _uvMax;
    }
    void SetUVs(const glm::vec2& min, const glm::vec2& max) {
        _uvMin = min;
        _uvMax = max;
    }

    bool GetFlipX() const {
        return _flipX;
    }
    bool GetFlipY() const {
        return _flipY;
    }
    void SetFlipX(bool flip) {
        _flipX = flip;
    }
    void SetFlipY(bool flip) {
        _flipY = flip;
    }

    int GetZOrder() const override {
        return _zOrder;
    }
    void SetZOrder(int zOrder) {
        _zOrder = zOrder;
    }

private:
    glm::vec2 _size;// Base size in pixels
    glm::vec2 _pivot;// Pivot point (0,0 = top-left, 1,1 = bottom-right)
    glm::vec4 _color;
    int _textureID;
    CanvasLayer _layer;
    glm::vec2 _uvMin = glm::vec2(0.0f, 0.0f);
    glm::vec2 _uvMax = glm::vec2(1.0f, 1.0f);
    bool _flipX = false;
    bool _flipY = false;
    int _zOrder = 0;
};