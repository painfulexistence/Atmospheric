#pragma once

#include "component.hpp"
#include "physics_server_2d.hpp"
#include <box2d/box2d.h>
#include <glm/vec2.hpp>
#include <vector>

// Body type enum matching Box2D
enum class BodyType2D { Static = 0, Kinematic = 1, Dynamic = 2 };

// Shape type for 2D physics
enum class ShapeType2D { Box, Circle, Polygon };

// Shape definition
struct Shape2DDef {
    ShapeType2D type = ShapeType2D::Box;

    // For Box
    glm::vec2 boxSize = glm::vec2(1.0f, 1.0f);

    // For Circle
    float circleRadius = 0.5f;

    // For Polygon (in pixels, will be converted to meters)
    std::vector<glm::vec2> polygonVertices;

    // Physics properties
    float density = 1.0f;
    float friction = 0.3f;
    float restitution = 0.0f;// Bounciness

    // Collision filtering
    bool isSensor = false;
};

struct Rigidbody2DProps {
    BodyType2D type = BodyType2D::Dynamic;
    Shape2DDef shape;

    // Initial state
    glm::vec2 position = glm::vec2(0.0f);
    float angle = 0.0f;// radians

    // Body properties
    float linearDamping = 0.0f;
    float angularDamping = 0.0f;
    float gravityScale = 1.0f;
    bool fixedRotation = false;
    bool bullet = false;// For fast moving objects
};

class Rigidbody2DComponent : public Component {
public:
    Rigidbody2DComponent(GameObject* gameObject, const Rigidbody2DProps& props);
    ~Rigidbody2DComponent() override;

    std::string GetName() const override {
        return "Rigidbody2DComponent";
    }

    void OnAttach() override;
    void OnDetach() override;

    bool CanTick() const override {
        return true;
    }

    void SyncToTransform(float dt);

    // Position and rotation (in pixels and radians)
    glm::vec2 GetPosition() const;
    void SetPosition(const glm::vec2& position);
    float GetAngle() const;
    void SetAngle(float angle);

    // Velocity
    glm::vec2 GetLinearVelocity() const;
    void SetLinearVelocity(const glm::vec2& velocity);
    float GetAngularVelocity() const;
    void SetAngularVelocity(float omega);

    // Forces and impulses (in pixels)
    void ApplyForce(const glm::vec2& force, const glm::vec2& point);
    void ApplyForceToCenter(const glm::vec2& force);
    void ApplyTorque(float torque);
    void ApplyLinearImpulse(const glm::vec2& impulse, const glm::vec2& point);
    void ApplyLinearImpulseToCenter(const glm::vec2& impulse);
    void ApplyAngularImpulse(float impulse);

    // Body type
    BodyType2D GetBodyType() const;
    void SetBodyType(BodyType2D type);

    // Properties
    void SetGravityScale(float scale);
    float GetGravityScale() const;

    void SetFixedRotation(bool fixed);
    bool IsFixedRotation() const;

    // Mass
    float GetMass() const;

    // Shape access (for debug drawing)
    const Shape2DDef& GetShapeDef() const {
        return _shapeDef;
    }

    // Box2D body access
    b2BodyId GetBodyId() {
        return _bodyId;
    }

    // User data for callbacks
    void* userData = nullptr;

private:
    void CreateBody();
    void DestroyBody();
    void CreateShape();

    Rigidbody2DProps _props;
    Shape2DDef _shapeDef;
    b2BodyId _bodyId;
};
