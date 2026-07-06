#pragma once
#include "component.hpp"
#include "globals.hpp"
#include <glm/vec3.hpp>

class Material;
class Mesh;

struct WaterProps {
    float width = 100.0f;
    float depth = 100.0f;
    int subdivisions = 64;
    float waveStrength = 0.1f;
    float waveSpeed = 1.0f;
    // Fog color is not configurable here -- WaterPass reads it live from
    // SkyboxPass::skyColor, matching VX.
    float fogDensity = 0.00001f;// VX: u_fog_density in scene.py render_water
    glm::vec3 deepColor = { 0.05f, 0.1f, 0.25f };// VX COLOR_INDIGO
    glm::vec3 shallowColor = { 0.686f, 0.933f, 0.933f };// VX COLOR_MINT_GREEN
    float beerCoef = 0.095f;
    // Planar reflection of the sky/terrain, rendered by PlanarReflectionPass
    // about the plane at the water's height.
    bool reflections = true;
    float reflectionStrength = 0.6f;
    float reflectionDistortion = 0.02f;// wave-normal-driven UV wobble
    // waterLine defaults to the owner's Y position at attach time.
    // Set explicitly to override (e.g., to account for a wave offset).
    float waterLine = -1e30f;// sentinel: use owner->GetPosition().y
};

// Creates a subdivided water plane as a transparent MeshComponent, and stores
// per-instance wave parameters in the material so WaterPass can read them.
class WaterComponent : public Component {
public:
    WaterComponent(GameObject* owner, const WaterProps& props = {});
    ~WaterComponent() = default;

    std::string GetName() const override {
        return "Water";
    }
    void OnAttach() override;
    void OnDetach() override {
    }
    void OnTick(float dt) override;
    void DrawImGui() override;

    Material* GetMaterial() const {
        return _material;
    }

private:
    MeshHandle _mesh;
    Material* _material = nullptr;
    WaterProps _props;
};
