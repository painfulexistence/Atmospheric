#include "shape_renderer_component.hpp"
#include "application.hpp"
#include "batch_renderer_2d.hpp"
#include "game_object.hpp"
#include "graphics_server.hpp"

ShapeRendererComponent::ShapeRendererComponent(GameObject* gameObject, const ShapeRendererProps& props)
  : CanvasDrawable(gameObject), _props(props) {
}

void ShapeRendererComponent::Draw(BatchRenderer2D* renderer) {
    glm::vec2 pos = gameObject->GetPosition();
    // glm::vec2 scale = gameObject->GetScale(); // Should apply scale?
    // Rigidbody debug draw didn't apply scale because body has size.
    // Here we use props size. Transform matrix handles position?
    // BatchRenderer2D methods often take pos/size directly in world space
    // OR local space if transform is passed?
    // DrawCircle takes (pos, radius, color).
    // Let's use world position from GameObject.

    // Draw functions in BatchRenderer2D:
    // DrawCircle(vec2 pos, float radius, vec4 color, float thickness)
    // DrawRect(vec3 pos, vec2 size, vec4 color, float thickness)
    // DrawPolygon(vec2* verts, count, color, thickness)

    // If Filled, it's different.
    // DrawCircleFilled(pos, radius, color)
    // DrawRectFilled(pos, size, color)
    // DrawTriangleFilled(...)

    // Note: RigidBody debug draw was using `renderer->DrawCircle(pos, radius...`

    if (_props.type == ShapeType2D::Circle) {
        if (_props.filled) {
            renderer->DrawCircleFilled(pos, _props.radius, _props.color);
        } else {
            renderer->DrawCircle(pos, _props.radius, _props.color, _props.thickness);
        }
    } else if (_props.type == ShapeType2D::Box) {
        glm::vec2 size = _props.boxHalfSize * 2.0f;
        if (_props.filled) {
            // DrawQuad uses center position
            renderer->DrawQuad(pos, size, _props.color);
        } else {
            // DrawRect uses top-left position
            glm::vec3 topLeft = glm::vec3(pos.x - _props.boxHalfSize.x, pos.y - _props.boxHalfSize.y, 0.0f);
            renderer->DrawRect(topLeft, size, _props.color, _props.thickness);
        }
    } else if (_props.type == ShapeType2D::Polygon) {
        glm::mat4 transform = gameObject->GetObjectTransform();
        std::vector<glm::vec2> worldVerts;
        worldVerts.reserve(_props.vertices.size());
        for (const auto& v : _props.vertices) {
            glm::vec4 v4 = transform * glm::vec4(v, 0, 1);
            worldVerts.push_back(glm::vec2(v4.x, v4.y));
        }

        if (_props.filled) {
            renderer->DrawPolygonFilled(worldVerts, _props.color);
        } else {
            renderer->DrawPolygon(worldVerts, _props.color, _props.thickness);
        }
    }
}

void ShapeRendererComponent::OnAttach() {
    gameObject->GetApp()->GetGraphicsServer()->RegisterCanvasDrawable(this);
}

void ShapeRendererComponent::OnDetach() {
}
