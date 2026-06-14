#pragma once
#include "canvas_drawable.hpp"
#include "globals.hpp"
#include "rigidbody_2d_component.hpp"// For ShapeType2D if reusing, or define own?
// Actually Rigidbody2DComponent defines Shape2DDef inside it? Or separate?
// Looking at previous view: definition was in rigidbody_2d_component.hpp
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <vector>

struct ShapeRendererProps {
    ShapeType2D type = ShapeType2D::Box;
    glm::vec4 color = glm::vec4(1.0f);
    float thickness = 1.0f;
    bool filled = false;

    // Circle
    float radius = 10.0f;

    // Box
    glm::vec2 boxHalfSize = glm::vec2(10.0f);

    // Polygon
    std::vector<glm::vec2> vertices;

    // Capsule/Segment (if needed)

    CanvasLayer layer = CanvasLayer::LAYER_WORLD_2D;
};

class ShapeRendererComponent : public CanvasDrawable {
public:
    ShapeRendererComponent(GameObject* gameObject, const ShapeRendererProps& props);

    std::string GetName() const override {
        return "ShapeRendererComponent";
    }

    void OnAttach() override;
    void OnDetach() override;

    void Draw(BatchRenderer2D* renderer) override;

    CanvasLayer GetLayer() const override {
        return _props.layer;
    }

    // Setters/Getters
    void SetColor(const glm::vec4& color) {
        _props.color = color;
    }
    glm::vec4 GetColor() const {
        return _props.color;
    }
    void SetRadius(float radius) {
        _props.radius = radius;
    }
    void SetBoxHalfSize(const glm::vec2& halfSize) {
        _props.boxHalfSize = halfSize;
    }

private:
    ShapeRendererProps _props;
};
