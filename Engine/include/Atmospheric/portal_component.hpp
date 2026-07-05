#pragma once
#include "component.hpp"
#include "globals.hpp"
#include <glm/vec3.hpp>

class Mesh;
class PortalMaterial;

struct PortalProps {
    float radius = 8.0f;
    int segments = 48;
    glm::vec3 rimColor = { 0.35f, 0.65f, 1.0f };
};

// A circular portal surface. The disc's local +Z is the side you look into and
// exit from; orient it with the owning GameObject's rotation. Link two portals
// with PortalComponent::Link — PortalPass then renders the recursive
// through-the-portal views the surface samples, and this component teleports
// the main camera Portal-style when it flies through the disc: position,
// facing (and hence apparent momentum) carry over relative to the exit portal.
class PortalComponent : public Component {
public:
    PortalComponent(GameObject* owner, const PortalProps& props = {});
    ~PortalComponent() = default;

    std::string GetName() const override {
        return "Portal";
    }
    void OnAttach() override {
    }
    void OnDetach() override;
    void OnTick(float dt) override;
    void DrawImGui() override;

    static void Link(PortalComponent* a, PortalComponent* b);

    PortalMaterial* GetMaterial() const {
        return _material;
    }

private:
    MeshHandle _mesh;
    PortalMaterial* _material = nullptr;
    PortalComponent* _linked = nullptr;
    PortalProps _props;

    // Previous-frame camera eye for the plane-crossing test.
    glm::vec3 _prevCamPos{ 0.0f };
    bool _prevValid = false;
};
