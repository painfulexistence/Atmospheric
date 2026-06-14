#include "camera_component.hpp"
#include "game_object.hpp"

static const float maxVAngle = PI / 2.0f - 0.01f;
static const float minVAngle = -PI / 2.0f + 0.01f;

CameraComponent::CameraComponent(GameObject* gameObject, const CameraProps& props) {
    this->gameObject = gameObject;
    if (props.isOrthographic) {
        _isOrthographic = true;
        _orthoWidth = props.orthographic.width;
        _orthoHeight = props.orthographic.height;
        _nearZ = props.orthographic.nearClip;
        _farZ = props.orthographic.farClip;
        _projectionMatrix = glm::ortho(
          -props.orthographic.width * .5f,
          props.orthographic.width * .5f,
          -props.orthographic.height * .5f,
          props.orthographic.height * .5f,
          props.orthographic.nearClip,
          props.orthographic.farClip
        );
    } else {
        _isOrthographic = false;
        _fov = props.perspective.fieldOfView;
        _aspectRatio = props.perspective.aspectRatio;
        _nearZ = props.perspective.nearClip;
        _farZ = props.perspective.farClip;
        _projectionMatrix = glm::perspective(
          props.perspective.fieldOfView,
          props.perspective.aspectRatio,
          props.perspective.nearClip,
          props.perspective.farClip
        );
    }
    _eyeOffset = props.eyeOffset;
    _vhAngle = glm::vec2(props.verticalAngle, props.horizontalAngle);
}

std::string CameraComponent::GetName() const {
    return std::string("Camera");
}

void CameraComponent::SetPerspective(float fov, float aspectRatio, float nearClip, float farClip) {
    _isOrthographic = false;
    _projectionMatrix = glm::perspective(fov, aspectRatio, nearClip, farClip);
}

void CameraComponent::SetOrthographic(float width, float height, float nearClip, float farClip) {
    _isOrthographic = true;
    _orthoWidth = width;
    _orthoHeight = height;
    _nearZ = nearClip;
    _farZ = farClip;
    _projectionMatrix = glm::ortho(-width * .5f, width * .5f, -height * .5f, height * .5f, nearClip, farClip);
}

glm::vec3 CameraComponent::GetEyePosition() {
    glm::mat4 xform = gameObject->GetObjectTransform();
    return glm::vec3(xform * glm::vec4(_eyeOffset, 1.0));
}

glm::vec3 CameraComponent::GetEyeDirection() {
    float vAngle = _vhAngle.x, hAngle = _vhAngle.y;
    return glm::vec3(glm::cos(vAngle) * glm::cos(hAngle), glm::sin(vAngle), glm::sin(hAngle) * glm::cos(vAngle));
}

glm::mat4 CameraComponent::GetViewMatrix() {
    glm::vec3 eyePos = GetEyePosition();
    return glm::lookAt(eyePos, eyePos + GetEyeDirection(), glm::vec3(0, 1, 0));
}

glm::mat4 CameraComponent::GetProjectionMatrix() {
    return _projectionMatrix;
}

glm::vec3 CameraComponent::GetMoveVector(Axis axis) {
    glm::vec3 dir = GetEyeDirection();
    switch (axis) {
    case BACK:
    case FRONT:
        return glm::normalize(glm::vec3(dir.x, 0, dir.z));
    case RIGHT:
    case LEFT:
        return glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)));
    case UP:
    case DOWN:
        return glm::vec3(0, 1, 0);
    default:
        throw std::runtime_error("Invalid camera move axis");
    }
}

void CameraComponent::Yaw(float angleOffset) {
    _vhAngle.y += angleOffset;
}

void CameraComponent::Pitch(float angleOffset) {
    _vhAngle.x = std::max(minVAngle, std::min(maxVAngle, _vhAngle.x + angleOffset));
}

void CameraComponent::SetSize(float size) {
    if (_isOrthographic) {
        float aspectRatio = _orthoWidth / _orthoHeight;// Maintain aspect ratio
        _orthoHeight = size;
        _orthoWidth = size * aspectRatio;
        _projectionMatrix =
          glm::ortho(-_orthoWidth * .5f, _orthoWidth * .5f, -_orthoHeight * .5f, _orthoHeight * .5f, _nearZ, _farZ);
    }
}
