#include "portal_component.hpp"
#include "asset_manager.hpp"
#include "camera_component.hpp"
#include "game_object.hpp"
#include "graphics_subsystem.hpp"
#include "imgui.h"
#include "material.hpp"
#include "mesh.hpp"
#include "mesh_renderer.hpp"

#include <glm/gtc/matrix_transform.hpp>

PortalComponent::PortalComponent(GameObject* owner, const PortalProps& props) : _props(props) {
    gameObject = owner;

    auto& am = AssetManager::Get();
    _mesh = am.CreateDiscMesh("Portal_" + owner->GetName(), props.radius, props.segments);

    _material = am.CreatePortalMaterial();
    _material->rimColor = props.rimColor;
    if (Mesh* meshPtr = am.GetMeshPtr(_mesh)) meshPtr->SetMaterial(am.GetMaterialHandle(_material));

    owner->AddComponent<MeshRenderer>(_mesh);
}

void PortalComponent::Link(PortalComponent* a, PortalComponent* b) {
    if (!a || !b || a == b) return;
    a->_linked = b;
    b->_linked = a;
    if (a->_material && b->_material) {
        a->_material->partner = b->_material;
        b->_material->partner = a->_material;
    }
}

void PortalComponent::OnDetach() {
    if (_linked) {
        _linked->_linked = nullptr;
        if (_linked->_material) _linked->_material->partner = nullptr;
        _linked = nullptr;
    }
    if (_material) _material->partner = nullptr;
}

void PortalComponent::OnTick(float /*dt*/) {
    if (!_linked) {
        _prevValid = false;
        return;
    }

    auto* gfx = GraphicsSubsystem::Get();
    CameraComponent* camera = gfx ? gfx->GetMainCamera() : nullptr;
    if (!camera) {
        _prevValid = false;
        return;
    }

    glm::vec3 curr = camera->GetEyePosition();

    if (_prevValid) {
        glm::mat4 mSelf = gameObject->GetTransform();
        glm::vec3 n = glm::normalize(glm::vec3(mSelf[2]));// portal faces local +Z
        glm::vec3 p0 = glm::vec3(mSelf[3]);

        // Crossed from the front side to the back side this frame?
        float dPrev = glm::dot(n, _prevCamPos - p0);
        float dCurr = glm::dot(n, curr - p0);
        if (dPrev > 0.0f && dCurr <= 0.0f) {
            float t = dPrev / (dPrev - dCurr);
            glm::vec3 hit = glm::mix(_prevCamPos, curr, t);
            if (glm::length(hit - p0) <= _props.radius) {
                // Portal-game transit: the same rigid transform PortalPass uses
                // for its virtual cameras. R180 about the portal's up maps
                // "into the front of self" onto "out of the front of partner",
                // carrying relative position and facing (apparent momentum).
                const glm::mat4 R180 = glm::rotate(glm::mat4(1.0f), 3.14159265359f, glm::vec3(0.0f, 1.0f, 0.0f));
                glm::mat4 T = _linked->gameObject->GetTransform() * R180 * glm::inverse(mSelf);

                glm::vec3 goPos = camera->gameObject->GetPosition();
                camera->gameObject->SetPosition(glm::vec3(T * glm::vec4(goPos, 1.0f)));
                camera->SetEyeDirection(glm::mat3(T) * camera->GetEyeDirection());

                // Both ends must re-arm: the camera jumped, so neither side's
                // previous position is meaningful for crossing tests.
                _linked->_prevValid = false;
                _prevValid = false;
                return;
            }
        }
    }

    _prevCamPos = curr;
    _prevValid = true;
}

void PortalComponent::DrawImGui() {
    if (!_material) return;
    ImGui::ColorEdit3("Rim Color", &_material->rimColor.x);
    ImGui::Text(_linked ? "Linked" : "Unlinked");
}
