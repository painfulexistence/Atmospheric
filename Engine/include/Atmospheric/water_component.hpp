#pragma once
#include "component.hpp"
#include "globals.hpp"
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
    glm::vec3 deepColor    = {0.04f,  0.11f,  0.35f};
    glm::vec3 shallowColor = {0.686f, 0.933f, 0.933f};
    float     beerCoef     =   0.095f;
    // waterLine defaults to the owner's Y position at attach time.
    // Set explicitly to override (e.g., to account for a wave offset).
    float     waterLine    = -1e30f;  // sentinel: use owner->GetPosition().y
};

// Creates a subdivided water plane as a transparent MeshComponent, and stores
// per-instance wave parameters in the material so WaterPass can read them.
class WaterComponent : public Component {
public:
    WaterComponent(GameObject* owner, const WaterProps& props = {});

    std::string GetName() const override { return "Water"; }
    void OnAttach() override;
    void OnDetach() override {}
    void OnTick(float dt) override;
    void DrawImGui() override;

    Material* GetMaterial() const { return _material; }

private:
    MeshHandle _mesh;
    Material*  _material = nullptr;
    WaterProps _props;
};
