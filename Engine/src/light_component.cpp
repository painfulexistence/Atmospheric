#include "light_component.hpp"
#include "game_object.hpp"

static glm::vec3 Direction(GLenum face) {
    switch (face) {
    case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
        return glm::vec3(1.f, 0.f, 0.f);
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
        return glm::vec3(-1.f, 0.f, 0.f);
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
        return glm::vec3(0.f, 1.f, 0.f);
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
        return glm::vec3(0.f, -1.f, 0.f);
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
        return glm::vec3(0.f, 0.f, 1.f);
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
        return glm::vec3(0.f, 0.f, -1.f);
    default:
        std::runtime_error("Invalid cube face");
        return glm::vec3(1.f, 0.f, 0.f);
    }
}

static glm::vec3 WorldUp(GLenum face) {
    switch (face) {
    case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
        return glm::vec3(0.f, -1.f, 0.f);
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
        return glm::vec3(0.f, -1.f, 0.f);
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
        return glm::vec3(0.f, 0.f, -1.f);
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
        return glm::vec3(0.f, 0.f, 1.f);
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
        return glm::vec3(0.f, -1.f, 0.f);
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
        return glm::vec3(0.f, -1.f, 0.f);
    default:
        std::runtime_error("Invalid cube face");
        return glm::vec3(0.f, -1.f, 0.f);
    }
}

LightComponent::LightComponent(GameObject* gameObject, LightProps props) {
    type = props.type;
    direction = props.direction;
    ambient = props.ambient;
    diffuse = props.diffuse;
    specular = props.specular;
    intensity = props.intensity;
    attenuation = props.attenuation;
    castShadow = props.castShadow;
}

std::string LightComponent::GetName() const {
    return std::string("Light");
}

glm::vec3 LightComponent::GetPosition() const {
    return gameObject->GetPosition();
}

glm::mat4 LightComponent::GetProjectionMatrix(int cascadedIndex) {
    if (type == LightType::Directional) {
        float nearZ = -200.0f, farZ = 200.0f;
        float orthoSize = 200.0f;
        return glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, nearZ, farZ);
    } else {
        // NOTES:
        //  Here a small farZ would be used to avoid unnecessary shadow calculation of cutoff point lights
        float nearZ = 0.1f, farZ = 400.0f;
        return glm::perspective(glm::radians(90.0f), 1.0f, nearZ, farZ);
    }
}

glm::mat4 LightComponent::GetViewMatrix(GLenum facing) {
    auto pos = GetPosition();
    if (type == LightType::Directional) {
        // NOTES:
        //  To simulate real world lighting, directional light should be placed as far as possible,
        //  but here the light would be placed depending on farZ to ensure the scene can completely fit itself into clip
        //  space
        float viewDist = 200.0f;
        glm::vec3 center = pos;
        return glm::lookAt(center - glm::normalize(direction) * viewDist, center, glm::vec3(0.f, 1.f, 0.f));
    } else {
        return glm::lookAt(pos, pos + Direction(facing), WorldUp(facing));
    }
}

glm::mat4 LightComponent::GetProjectionViewMatrix(int cascadedIndex, GLenum facing) {
    return GetProjectionMatrix(cascadedIndex) * GetViewMatrix(facing);
}
