#include "transform_component.hpp"
#include "game_object.hpp"

TransformComponent::TransformComponent(GameObject* owner, glm::vec3 position, glm::vec3 rotation, glm::vec3 scale) {
    gameObject = owner;
    _position = position;
    _rotation = rotation;
    _scale = scale;
    UpdateTransform();
}

void TransformComponent::SetPosition(const glm::vec3& position) {
    _position = position;
    UpdateTransform();
}

void TransformComponent::SetRotation(const glm::vec3& rotation) {
    _rotation = rotation;
    UpdateTransform();
}

void TransformComponent::SetScale(const glm::vec3& scale) {
    _scale = scale;
    UpdateTransform();
}

glm::mat4 TransformComponent::GetLocalTransform() const {
    return _localToWorld;
}

glm::mat4 TransformComponent::GetWorldTransform() const {
    if (gameObject && gameObject->parent) {
        return gameObject->parent->GetTransform() * _localToWorld;
    }
    return _localToWorld;
}

void TransformComponent::SetLocalTransform(const glm::mat4& transform) {
    _localToWorld = transform;
    UpdatePositionRotationScale();
}

void TransformComponent::SetWorldTransform(const glm::mat4& transform) {
    if (gameObject && gameObject->parent) {
        glm::mat4 parentTransform = gameObject->parent->GetTransform();
        _localToWorld = glm::inverse(parentTransform) * transform;
    } else {
        _localToWorld = transform;
    }
    UpdatePositionRotationScale();
}

void TransformComponent::SyncWorldTransform(const glm::mat4& transform) {
    SetWorldTransform(transform);
}

// Update local matrix from P/R/S
void TransformComponent::UpdateTransform() {
    _localToWorld = glm::translate(glm::mat4(1.0f), _position) * glm::mat4_cast(glm::quat(_rotation))
                    * glm::scale(glm::mat4(1.0f), _scale);
}

// Update P/R/S from local matrix
void TransformComponent::UpdatePositionRotationScale() {
    _position = glm::vec3(_localToWorld[3]);
    _rotation = glm::eulerAngles(glm::quat_cast(glm::mat3(_localToWorld)));
    _scale = glm::vec3(glm::length(_localToWorld[0]), glm::length(_localToWorld[1]), glm::length(_localToWorld[2]));
}

// User-friendly API using degrees
void TransformComponent::SetEulerAngles(const glm::vec3& degrees) {
    _rotation = glm::radians(degrees);
    UpdateTransform();
}

glm::vec3 TransformComponent::GetEulerAngles() const {
    return glm::degrees(_rotation);
}
