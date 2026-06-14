#pragma once
#include "Atmospheric.hpp"
#include "Atmospheric/camera_component.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Camera controllers
// ─────────────────────────────────────────────────────────────────────────────

namespace axex {

// World-axis "fly" camera controller. Attach it to the GameObject that owns the
// CameraComponent and it will drive both translation and look angles from input:
//
//   WASD  : move along the world X/Z plane
//   R / F : move up / down (world Y)
//   IJKL  : look (pitch / yaw)
//
// Movement is deliberately world-aligned (not view-relative) so it stays
// predictable for spectator / debug fly-throughs.
class FlyCameraComponent : public Component {
public:
    // moveSpeed: units/second, lookSpeed: radians/second.
    FlyCameraComponent(GameObject* go, float moveSpeed = 20.0f, float lookSpeed = 1.5f)
        : _moveSpeed(moveSpeed), _lookSpeed(lookSpeed) {
    }

    std::string GetName() const override {
        return "FlyCameraComponent";
    }

    void OnAttach() override {
        _camera = gameObject->GetComponent<CameraComponent>();
    }

    void OnTick(float dt) override {
        Input* input = gameObject->GetApp()->GetInput();
        if (!input) return;

        const float move = _moveSpeed * dt;
        const float look = _lookSpeed * dt;

        if (_camera) {
            if (input->IsKeyDown(Key::I)) _camera->Pitch(look);
            if (input->IsKeyDown(Key::K)) _camera->Pitch(-look);
            if (input->IsKeyDown(Key::J)) _camera->Yaw(-look);
            if (input->IsKeyDown(Key::L)) _camera->Yaw(look);
        }

        glm::vec3 pos = gameObject->GetPosition();
        if (input->IsKeyDown(Key::W)) pos.z -= move;
        if (input->IsKeyDown(Key::S)) pos.z += move;
        if (input->IsKeyDown(Key::A)) pos.x -= move;
        if (input->IsKeyDown(Key::D)) pos.x += move;
        if (input->IsKeyDown(Key::R)) pos.y += move;
        if (input->IsKeyDown(Key::F)) pos.y -= move;
        gameObject->SetPosition(pos);
    }

private:
    float _moveSpeed;
    float _lookSpeed;
    CameraComponent* _camera = nullptr;
};

}// namespace axex
