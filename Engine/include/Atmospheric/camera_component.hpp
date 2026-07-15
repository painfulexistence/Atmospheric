#pragma once
#include "component.hpp"
#include "frustum.hpp"
#include "globals.hpp"
#include <optional>

struct CameraProps {
    bool isOrthographic = false;
    union {
        struct {
            float fieldOfView = 58.0f;// vertical fov in degrees (was an effective ~58 deg
            // back when fov was mistakenly passed to glm::perspective as radians)
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
        perspective = { 58.0f, 1.333f, 0.1f, 500.0f };
        isOrthographic = false;
    }
};

class CameraComponent : public Component {
public:
    CameraComponent(GameObject* gameObject, const CameraProps& props);

    std::string GetName() const override;

    void OnAttach() override;
    void OnDetach() override;
    void DrawImGui() override;

    // fov is the vertical field of view in degrees (converted to radians
    // internally, like CameraProps::perspective::fieldOfView).
    void SetPerspective(float fov, float aspectRatio, float nearClip, float farClip);

    void SetOrthographic(float width, float height, float nearClip, float farClip);

    bool IsOrthographic() const {
        return _isOrthographic;
    }

    glm::vec3 GetEyePosition();

    glm::vec3 GetEyeDirection();

    glm::mat4 GetViewMatrix();

    glm::mat4 GetProjectionMatrix();

    // Cached view frustum. Rebuilt lazily when the projection or the derived
    // view-projection matrix changes (bit-compared, so a stationary camera
    // pays only the compare). Callers hold the reference for the frame.
    const Frustum& GetViewFrustum();

    glm::vec3 GetMoveVector(Axis);

    void Yaw(float);

    void Pitch(float);

    // Set the view direction directly (world space); yaw/pitch are derived.
    // Used by portal teleports to carry the camera's facing through a portal.
    void SetEyeDirection(const glm::vec3& dir);

    void SetSize(float size);

private:
    float _fov = 45.0f;
    float _aspectRatio = 1.0f;
    float _nearZ = 0.1f;
    float _farZ = 1000.0f;
    float _orthoWidth = 10.0f;
    float _orthoHeight = 10.0f;
    bool _isOrthographic = false;
    glm::vec3 _eyeOffset = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec2 _vhAngle = glm::vec2(0.0f, 0.0f);
    glm::mat4 _projectionMatrix;
    glm::mat4 _viewMatrix;

    // Frustum cache. _cachedFrustumVP is the VP the current _cachedFrustum was
    // built from; a bit mismatch (camera moved or projection changed) forces
    // a rebuild. Optional so the first access always builds.
    std::optional<Frustum> _cachedFrustum;
    glm::mat4 _cachedFrustumVP{ 0.0f };
};
