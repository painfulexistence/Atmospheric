#pragma once
#include "component.hpp"
#include <glm/vec3.hpp>

class Material;
class Mesh;

struct WaterProps {
    float     width        = 100.0f;
    float     depth        = 100.0f;
    int       subdivisions =    64;
    float     waveStrength =   0.1f;
    float     waveSpeed    =   1.0f;
    glm::vec3 fogColor     = {0.55f, 0.65f, 0.75f};
    float     fogDensity   =   0.003f;
    // waterLine defaults to the owner's Y position at attach time.
    // Set explicitly to override (e.g., to account for a wave offset).
    float     waterLine    = -1e30f;  // sentinel: use owner->GetPosition().y
};

// Creates a subdivided water plane as a transparent MeshComponent, and stores
// per-instance wave parameters in the material so WaterPass can read them.
class WaterComponent : public Component {
public:
    WaterComponent(GameObject* owner, const WaterProps& props = {});
    ~WaterComponent();

    std::string GetName() const override { return "Water"; }
    void OnAttach() override;
    void OnDetach() override {}

    Material* GetMaterial() const { return _material; }

private:
    Mesh*      _mesh     = nullptr;
    Material*  _material = nullptr;
    WaterProps _props;
};
