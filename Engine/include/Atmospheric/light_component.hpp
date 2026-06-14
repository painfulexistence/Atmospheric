#pragma once
#include "component.hpp"
#include "globals.hpp"
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

enum class LightType { Directional = 0, Point = 1, Spot = 2, Area = 3 };

struct LightProps {
    // Color reference: http://planetpixelemporium.com/tutorialpages/light.html
    // Attenuation reference: http://wiki.ogre3d.org/tiki-index.php?page=-Point+Light+Attenuation
    LightType type;
    glm::vec3 ambient;
    glm::vec3 diffuse;
    glm::vec3 specular;
    glm::vec3 direction;
    glm::vec3 attenuation;
    float intensity;
    bool castShadow;
};

class LightComponent : public Component {
public:
    LightType type;
    glm::vec3 direction;
    glm::vec3 attenuation;
    glm::vec3 ambient;
    glm::vec3 diffuse;
    glm::vec3 specular;
    float intensity;
    bool castShadow;

    LightComponent(GameObject* gameObject, LightProps props);

    std::string GetName() const override;

    glm::vec3 GetPosition() const;

    glm::mat4 GetProjectionMatrix(int cascadedIndex = 0);

    glm::mat4 GetViewMatrix(GLenum facing = GL_TEXTURE_CUBE_MAP_POSITIVE_X);

    glm::mat4 GetProjectionViewMatrix(int cascadedIndex = 0, GLenum facing = GL_TEXTURE_CUBE_MAP_POSITIVE_X);
};