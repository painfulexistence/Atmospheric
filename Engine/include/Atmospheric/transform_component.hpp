#pragma once
#include "component.hpp"
#include "globals.hpp"

class TransformComponent : public Component {
public:
    TransformComponent(GameObject* owner, glm::vec3 position, glm::vec3 rotation, glm::vec3 scale);
    ~TransformComponent() = default;

    std::string GetName() const override {
        return "TransformComponent";
    }

    // Position
    inline glm::vec3 GetPosition() const {
        return _position;
    }
    void SetPosition(const glm::vec3& position);

    // Rotation (radians)
    inline glm::vec3 GetRotation() const {
        return _rotation;
    }
    void SetRotation(const glm::vec3& rotation);

    // Rotation (degrees) - user-friendly API
    glm::vec3 GetEulerAngles() const;
    void SetEulerAngles(const glm::vec3& degrees);

    // Scale
    inline glm::vec3 GetScale() const {
        return _scale;
    }
    void SetScale(const glm::vec3& scale);

    // Transform matrices
    glm::mat4 GetLocalTransform() const;
    glm::mat4 GetWorldTransform() const;

    void SetLocalTransform(const glm::mat4& transform);
    void SetWorldTransform(const glm::mat4& transform);
    void SyncWorldTransform(const glm::mat4& transform);

private:
    glm::vec3 _position{ 0, 0, 0 };
    glm::vec3 _rotation{ 0, 0, 0 };
    glm::vec3 _scale{ 1, 1, 1 };

    glm::mat4 _localToWorld{ 1.0f };
    glm::mat4 _worldToWorld{ 1.0f };

    bool _isDirty = false;

    void UpdateTransform();
    void UpdatePositionRotationScale();
};
