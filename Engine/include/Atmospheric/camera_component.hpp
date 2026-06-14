#pragma once
#include "component.hpp"
#include "globals.hpp"

struct CameraProps {
    bool isOrthographic = false;
    union {
        struct {
            float fieldOfView = 45.0f;
            float aspectRatio = 1.333f;
            float nearClip = 0.1f;
            float farClip = 500.0f;
        } perspective;
        struct {
            float width = 500.0f;
            float height = 500.0f;
            float nearClip = -1.0f;
            float farClip = 1.0f;
        } orthographic;
    };
    float verticalAngle = 0.0f;
    float horizontalAngle = 0.0f;
    glm::vec3 eyeOffset = glm::vec3(0.0f);

    CameraProps() {
        perspective = { 45.0f, 1.333f, 0.1f, 500.0f };
        isOrthographic = false;
    }
};

class CameraComponent : public Component {
public:
    CameraComponent(GameObject* gameObject, const CameraProps& props);

    std::string GetName() const override;

    void SetPerspective(float fov, float aspectRatio, float nearClip, float farClip);

    void SetOrthographic(float width, float height, float nearClip, float farClip);

    bool IsOrthographic() const {
        return _isOrthographic;
    }

    glm::vec3 GetEyePosition();

    glm::vec3 GetEyeDirection();

    glm::mat4 GetViewMatrix();

    glm::mat4 GetProjectionMatrix();

    glm::vec3 GetMoveVector(Axis);

    void Yaw(float);

    void Pitch(float);

    void SetSize(float size);

private:
    float _fov;
    float _aspectRatio;
    float _nearZ;
    float _farZ;
    float _orthoWidth = 10.0f;
    float _orthoHeight = 10.0f;
    bool _isOrthographic = false;
    glm::vec3 _eyeOffset = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec2 _vhAngle = glm::vec2(0.0f, 0.0f);
    glm::mat4 _projectionMatrix;
    glm::mat4 _viewMatrix;
};
