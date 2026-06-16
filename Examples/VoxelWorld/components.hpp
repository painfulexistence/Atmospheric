#pragma once
#include "Atmospheric.hpp"
#include "Atmospheric/camera_component.hpp"

// World-axis fly camera. WASD move, R/F up/down, IJKL pitch/yaw.
class FlyCameraComponent : public Component {
    float _moveSpeed, _lookSpeed;
    CameraComponent* _camera = nullptr;
public:
    FlyCameraComponent(GameObject* go, float moveSpeed = 20.0f, float lookSpeed = 1.5f)
        : _moveSpeed(moveSpeed), _lookSpeed(lookSpeed) { gameObject = go; }
    std::string GetName() const override { return "FlyCameraComponent"; }
    void OnAttach() override { _camera = gameObject->GetComponent<CameraComponent>(); }
    void OnTick(float dt) override {
        auto* input = gameObject->GetApp()->GetInput();
        if (!input) return;
        const float move = _moveSpeed * dt, look = _lookSpeed * dt;
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
};
