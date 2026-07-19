#pragma once
#include "camera_component.hpp"
#include "game_object.hpp"
#include "input_subsystem.hpp"
#include "ui_page_component.hpp"
#include "window.hpp"
#include <RmlUi/Core/Element.h>
#include <algorithm>
#include <cstdint>
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <string>

// Touch fly-camera controls for mobile builds. RmlUi draws the transparent
// overlay (crosshair, floating joystick, action button — assets/ui/
// mobile_hud.rml) while raw multi-touch from InputSubsystem drives it —
// RmlUi's cursor model is single-pointer (SDL only synthesizes a mouse from
// the first finger), so fingers are classified and hit-tested here instead,
// which is what lets move + look + action happen simultaneously:
//   - a finger starting on the action button       → action (IsActionHeld())
//   - a finger starting in the bottom-left quarter → floating move joystick
//     (the stick centers where the finger lands)
//   - any other finger                             → look drag
// Layout is computed from the window's logical size (screen fractions, not
// fixed px) so the controls stay thumb-sized on every density. Attach to the
// camera's GameObject — movement follows the camera's facing, like
// CameraController3D's fly camera:
//
//   camGO->AddComponent<TouchControlsComponent>(/*moveSpeed=*/6.0f,
//                                              /*lookSpeed=*/2.6f, "DIG");
//
// Pass an empty actionLabel to hide the button entirely.
class TouchControlsComponent : public UIPageComponent {
public:
    TouchControlsComponent(
        GameObject* owner, float moveSpeed = 6.0f, float lookSpeed = 2.6f, std::string actionLabel = "DIG"
    )
      : UIPageComponent(owner, "assets/ui/mobile_hud.rml"),
        _moveSpeed(moveSpeed),
        _lookSpeed(lookSpeed),
        _actionLabel(std::move(actionLabel)) {
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
        _actionBtn = GetElement("action_btn");
        if (_actionBtn) {
            if (_actionLabel.empty()) {
                _actionBtn->SetProperty("display", "none");
            } else {
                _actionBtn->SetInnerRML(_actionLabel);
            }
        }
    }

    bool IsActionHeld() const {
        return _actionFinger != kNoFinger;
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
        if (_actionFinger != kNoFinger && !find(_actionFinger)) {
            _actionFinger = kNoFinger;
            if (_actionBtn) _actionBtn->SetClass("pressed", false);
        }

        // Claim new fingers. Classification uses the landing position only, so
        // a role once taken is never stolen by a later finger.
        for (const auto& t : touches) {
            if (t.id == _moveFinger || t.id == _lookFinger || t.id == _actionFinger) continue;
            const glm::vec2 start = t.startPosition * toLogical;
            if (InActionButton(start)) {
                if (_actionFinger == kNoFinger) {
                    _actionFinger = t.id;
                    if (_actionBtn) _actionBtn->SetClass("pressed", true);
                }
            } else if (start.x < 0.5f * logical.width && start.y > 0.42f * logical.height) {
                if (_moveFinger == kNoFinger) _moveFinger = t.id;
            } else if (_lookFinger == kNoFinger) {
                _lookFinger = t.id;
                _lastLookPos = t.position * toLogical;
            }
        }

        // Look: drag anywhere outside the joystick zone and action button.
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

        _actionSize = 0.21f * minDim;
        // Center such that the button's right/bottom edges sit at the margins.
        _actionCenter = { width - 0.075f * width - 0.5f * _actionSize, height - 0.16f * height - 0.5f * _actionSize };

        if (_actionBtn && !_actionLabel.empty()) {
            PlaceCircle(_actionBtn, _actionCenter, _actionSize);
            _actionBtn->SetProperty("font-size", fmt::format("{}px", 0.26f * _actionSize));
            _actionBtn->SetProperty("line-height", fmt::format("{}px", _actionSize));
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

    bool InActionButton(glm::vec2 logicalPos) const {
        if (_actionLabel.empty()) return false;
        // Circle test with a little slop so imprecise thumbs still register.
        return glm::length(logicalPos - _actionCenter) <= 0.5f * _actionSize + 10.0f;
    }

    CameraComponent* _camera = nullptr;
    Rml::Element* _crosshair = nullptr;
    Rml::Element* _base = nullptr;
    Rml::Element* _knob = nullptr;
    Rml::Element* _actionBtn = nullptr;

    float _moveSpeed;
    float _lookSpeed;
    std::string _actionLabel;

    int64_t _moveFinger = kNoFinger;
    int64_t _lookFinger = kNoFinger;
    int64_t _actionFinger = kNoFinger;
    glm::vec2 _lastLookPos{ 0.0f };

    glm::vec2 _layoutSize{ 0.0f };
    glm::vec2 _restingCenter{ 0.0f };
    glm::vec2 _actionCenter{ 0.0f };
    float _baseSize = 0.0f;
    float _knobSize = 0.0f;
    float _actionSize = 0.0f;
};
