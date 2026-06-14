#pragma once
#include "component.hpp"
#include <glm/vec3.hpp>

// Visual properties of the sun billboard. Attach alongside a LightComponent
// on the same GameObject — SunPass reads both.
class SunComponent : public Component {
public:
    SunComponent(glm::vec3 billboardColor = glm::vec3(1.0f, 0.4f, 0.0f) * 50.0f,
                 float     billboardRadius = 20.0f,
                 float     height          = 64.0f);

    std::string GetName() const override { return "SunComponent"; }
    void OnAttach() override;
    void OnDetach() override;

    glm::vec3 billboardColor  = glm::vec3(1.0f, 0.4f, 0.0f) * 50.0f;
    float     billboardRadius = 20.0f;
    float     height          = 64.0f;
};
