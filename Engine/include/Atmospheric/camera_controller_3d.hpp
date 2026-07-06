#pragma once
#include "camera_component.hpp"
#include "component.hpp"
#include "game_object.hpp"
#include "input_subsystem.hpp"
#include <glm/glm.hpp>

// View-relative fly camera. WASD moves along the camera's facing direction,
// R/F lifts/drops along world Y, arrow keys pitch/yaw, Z held slows movement
// by `slowMultiplier`. No-ops if the owning GameObject has no CameraComponent.
class CameraController3D : public Component {
public:
    CameraController3D(GameObject* owner, float moveSpeed = 20.0f, float lookSpeed = 1.5f, float slowMultiplier = 0.2f)
      : _moveSpeed(moveSpeed), _lookSpeed(lookSpeed), _slowMultiplier(slowMultiplier) {
        gameObject = owner;
    }

    std::string GetName() const override {
        return "CameraController3D";
    }

    void OnAttach() override {
        _camera = gameObject->GetComponent<CameraComponent>();
    }

    void OnTick(float dt) override {
        if (!_camera) return;
        auto* input = InputSubsystem::Get();
        if (!input) return;

        const float look = _lookSpeed * dt;
        if (input->IsKeyDown(Key::UP)) _camera->Pitch(look);
        if (input->IsKeyDown(Key::DOWN)) _camera->Pitch(-look);
        if (input->IsKeyDown(Key::RIGHT)) _camera->Yaw(look);
        if (input->IsKeyDown(Key::LEFT)) _camera->Yaw(-look);

        const float speed = _moveSpeed * (input->IsKeyDown(Key::Z) ? _slowMultiplier : 1.0f) * dt;
        const glm::vec3 fwd = _camera->GetEyeDirection();
        const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));

        glm::vec3 pos = gameObject->GetPosition();
        if (input->IsKeyDown(Key::W)) pos += fwd * speed;
        if (input->IsKeyDown(Key::S)) pos -= fwd * speed;
        if (input->IsKeyDown(Key::A)) pos -= right * speed;
        if (input->IsKeyDown(Key::D)) pos += right * speed;
        if (input->IsKeyDown(Key::R)) pos.y += speed;
        if (input->IsKeyDown(Key::F)) pos.y -= speed;
        gameObject->SetPosition(pos);
    }

private:
    CameraComponent* _camera = nullptr;
    float _moveSpeed;
    float _lookSpeed;
    float _slowMultiplier;
};
