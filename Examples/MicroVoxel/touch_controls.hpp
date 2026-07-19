#pragma once
#include "Atmospheric/camera_component.hpp"
#include "Atmospheric/game_object.hpp"
#include "Atmospheric/input_subsystem.hpp"
#include "Atmospheric/ui_page_component.hpp"
#include "Atmospheric/window.hpp"
#include <RmlUi/Core/Element.h>
#include <algorithm>
#include <cstdint>
#include <fmt/format.h>
#include <glm/glm.hpp>

// Touch controls for the mobile build. RmlUi draws the transparent overlay
// (crosshair, floating joystick, dig button) while raw multi-touch from
// InputSubsystem drives it — RmlUi's cursor model is single-pointer (SDL only
// synthesizes a mouse from the first finger), so fingers are classified and
// hit-tested here instead, which is what lets move + look + dig happen
// simultaneously:
//   - a finger starting on the dig button          → dig   (IsDigHeld())
//   - a finger starting in the bottom-left quarter → floating move joystick
//     (the stick centers where the finger lands)
//   - any other finger                             → look drag
// Layout is computed every frame from the window's logical size (screen
// fractions, not fixed px) so the controls stay thumb-sized on every density.
// Attach to the camera's GameObject — movement follows the camera's facing,
// like CameraController3D's fly camera.
class TouchControlsComponent : public UIPageComponent {
public:
    TouchControlsComponent(GameObject* owner, float moveSpeed = 6.0f, float lookSpeed = 2.6f)
      : UIPageComponent(owner, "assets/ui/mobile_hud.rml"), _moveSpeed(moveSpeed), _lookSpeed(lookSpeed) {
    }

    std::string GetName() const override {
        return "TouchControls";
    }

    void OnAttach() override {
        UIPageComponent::OnAttach();
        _camera = gameObject->GetComponent<CameraComponent>();
        _crosshair = GetElement("crosshair");
        _base = GetElement("vjoy_base");
        _knob = GetElement("vjoy_knob");
        _digBtn = GetElement("dig_btn");
    }

    bool IsDigHeld() const {
        return _digFinger != kNoFinger;
    }

    void OnTick(float dt) override {
        auto* input = InputSubsystem::Get();
        auto* window = Window::Get();
        if (!input || !window) return;

        const auto logical = window->GetLogicalSize();
        if (logical.width <= 0 || logical.height <= 0) return;
        const glm::vec2 dpi = window->GetDPI();
        const glm::vec2 toLogical(dpi.x > 0.0f ? 1.0f / dpi.x : 1.0f, dpi.y > 0.0f ? 1.0f / dpi.y : 1.0f);

        LayoutOverlay(static_cast<float>(logical.width), static_cast<float>(logical.height));

        const auto& touches = input->GetTouches();
        auto find = [&touches](int64_t id) -> const TouchPoint* {
            for (const auto& t : touches)
                if (t.id == id) return &t;
            return nullptr;
        };

        // Release roles whose finger lifted.
        if (_moveFinger != kNoFinger && !find(_moveFinger)) _moveFinger = kNoFinger;
        if (_lookFinger != kNoFinger && !find(_lookFinger)) _lookFinger = kNoFinger;
        if (_digFinger != kNoFinger && !find(_digFinger)) {
            _digFinger = kNoFinger;
            if (_digBtn) _digBtn->SetClass("pressed", false);
        }

        // Claim new fingers. Classification uses the landing position only, so
        // a role once taken is never stolen by a later finger.
        for (const auto& t : touches) {
            if (t.id == _moveFinger || t.id == _lookFinger || t.id == _digFinger) continue;
            const glm::vec2 start = t.startPosition * toLogical;
            if (InDigButton(start)) {
                if (_digFinger == kNoFinger) {
                    _digFinger = t.id;
                    if (_digBtn) _digBtn->SetClass("pressed", true);
                }
            } else if (start.x < 0.5f * logical.width && start.y > 0.42f * logical.height) {
                if (_moveFinger == kNoFinger) _moveFinger = t.id;
            } else if (_lookFinger == kNoFinger) {
                _lookFinger = t.id;
                _lastLookPos = t.position * toLogical;
            }
        }

        // Look: drag anywhere outside the joystick zone and dig button.
        if (_lookFinger != kNoFinger && _camera) {
            const TouchPoint* t = find(_lookFinger);
            const glm::vec2 pos = t->position * toLogical;
            const glm::vec2 delta = pos - _lastLookPos;
            _lastLookPos = pos;
            // _lookSpeed radians per screen-height of drag, aspect-independent.
            const float sensitivity = _lookSpeed / static_cast<float>(logical.height);
            _camera->Yaw(delta.x * sensitivity);
            _camera->Pitch(-delta.y * sensitivity);
        }

        // Move: fly toward the camera's facing, like CameraController3D.
        glm::vec2 knobOffset(0.0f);
        if (_moveFinger != kNoFinger) {
            const TouchPoint* t = find(_moveFinger);
            const glm::vec2 drag = (t->position - t->startPosition) * toLogical;
            const float radius = 0.5f * _baseSize - 0.5f * _knobSize;// knob stays inside the base ring
            glm::vec2 axis = radius > 0.0f ? drag / radius : glm::vec2(0.0f);
            const float len = glm::length(axis);
            if (len > 1.0f) axis /= len;
            knobOffset = axis * radius;
            if (len < 0.12f) axis = glm::vec2(0.0f);// deadzone
            if (_camera) {
                const glm::vec3 fwd = _camera->GetEyeDirection();
                const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
                glm::vec3 pos = gameObject->GetPosition();
                pos += (fwd * (-axis.y) + right * axis.x) * (_moveSpeed * dt);
                gameObject->SetPosition(pos);
            }
        }

        // Joystick visuals: float to the active finger's anchor, rest at the
        // default spot otherwise.
        glm::vec2 baseCenter = _restingCenter;
        if (_moveFinger != kNoFinger) {
            if (const TouchPoint* t = find(_moveFinger)) baseCenter = t->startPosition * toLogical;
        }
        PlaceCircle(_base, baseCenter, _baseSize);
        PlaceCircle(_knob, glm::vec2(0.5f * _baseSize) + knobOffset, _knobSize);// relative to the base
    }

private:
    static constexpr int64_t kNoFinger = -1;

    // Recompute the overlay layout when the logical size changes (first frame,
    // rotation). Everything scales off the screen so it works on any density.
    void LayoutOverlay(float width, float height) {
        if (width == _layoutSize.x && height == _layoutSize.y) return;
        _layoutSize = { width, height };
        const float minDim = std::min(width, height);

        _baseSize = 0.30f * minDim;
        _knobSize = 0.42f * _baseSize;
        _restingCenter = { 0.16f * width, height - 0.24f * height };

        _digSize = 0.21f * minDim;
        // Center such that the button's right/bottom edges sit at the margins.
        _digCenter = { width - 0.075f * width - 0.5f * _digSize, height - 0.16f * height - 0.5f * _digSize };

        if (_digBtn) {
            PlaceCircle(_digBtn, _digCenter, _digSize);
            _digBtn->SetProperty("font-size", fmt::format("{}px", 0.26f * _digSize));
            _digBtn->SetProperty("line-height", fmt::format("{}px", _digSize));
        }
        if (_crosshair) {
            const float size = 0.055f * minDim;
            PlaceCircle(_crosshair, { 0.5f * width, 0.5f * height }, size);
            _crosshair->SetProperty("font-size", fmt::format("{}px", size));
            _crosshair->SetProperty("line-height", fmt::format("{}px", size));
        }
    }

    // Position an element as a circle: top-left from center, square size,
    // fully-rounded corners. Sizes/positions are logical px (RmlUi's unit).
    static void PlaceCircle(Rml::Element* element, glm::vec2 center, float size) {
        if (!element) return;
        element->SetProperty("left", fmt::format("{}px", center.x - 0.5f * size));
        element->SetProperty("top", fmt::format("{}px", center.y - 0.5f * size));
        element->SetProperty("width", fmt::format("{}px", size));
        element->SetProperty("height", fmt::format("{}px", size));
        element->SetProperty("border-radius", fmt::format("{}px", 0.5f * size));
    }

    bool InDigButton(glm::vec2 logicalPos) const {
        // Circle test with a little slop so imprecise thumbs still register.
        return glm::length(logicalPos - _digCenter) <= 0.5f * _digSize + 10.0f;
    }

    CameraComponent* _camera = nullptr;
    Rml::Element* _crosshair = nullptr;
    Rml::Element* _base = nullptr;
    Rml::Element* _knob = nullptr;
    Rml::Element* _digBtn = nullptr;

    float _moveSpeed;
    float _lookSpeed;

    int64_t _moveFinger = kNoFinger;
    int64_t _lookFinger = kNoFinger;
    int64_t _digFinger = kNoFinger;
    glm::vec2 _lastLookPos{ 0.0f };

    glm::vec2 _layoutSize{ 0.0f };
    glm::vec2 _restingCenter{ 0.0f };
    glm::vec2 _digCenter{ 0.0f };
    float _baseSize = 0.0f;
    float _knobSize = 0.0f;
    float _digSize = 0.0f;
};
